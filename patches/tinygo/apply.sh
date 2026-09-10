#!/usr/bin/env bash
# Apply ESP32-S3 UART + cores-scheduler patches to TinyGo (default: brew).
# Cores: tinygo-org/tinygo#5358 adapted for 0.42; crosscore IRQ=14.
set -euo pipefail
ROOT="${1:-$(tinygo env TINYGOROOT)}"
HERE="$(cd "$(dirname "$0")" && pwd)"
TARGET="$ROOT/src/machine/machine_esp32s3.go"
BLOCK="$HERE/uart_block.go.inc"

if [[ ! -f "$TARGET" ]]; then
  echo "not found: $TARGET" >&2
  exit 1
fi
if [[ ! -f "$BLOCK" ]]; then
  echo "not found: $BLOCK" >&2
  exit 1
fi

# --- UART ---
python3 - "$TARGET" "$BLOCK" <<'PY'
import sys
from pathlib import Path

target = Path(sys.argv[1])
block = Path(sys.argv[2]).read_text()
text = target.read_text()

start = text.find("var DefaultUART = UART0")
if start < 0:
    sys.exit("marker not found: var DefaultUART = UART0")
end = text.find("\n// GetRNG returns", start)
if end < 0:
    end = text.find("\nfunc GetRNG", start)
if end < 0:
    sys.exit("marker not found: GetRNG after UART block")

bak = target.with_suffix(target.suffix + ".bak-uart")
if not bak.exists():
    bak.write_text(text)
    print(f"backup: {bak}")

new = text[:start] + block.rstrip() + "\n\n" + text[end+1:]
target.write_text(new)
print(f"patched: {target} (uart)")
PY

# --- cores: runtime file ---
CORES_DST="$ROOT/src/runtime/runtime_esp32s3_cores.go"
cp "$HERE/runtime_esp32s3_cores.go" "$CORES_DST"
printchmod() { chmod u+w "$1" 2>/dev/null || true; }
printchmod "$CORES_DST"
printchmod "$ROOT/src/device/esp/esp32s3.S"
printchmod "$ROOT/src/internal/task/task_stack_esp32.go"
printchmod "$ROOT/targets/esp32s3.ld"
printchmod "$ROOT/src/machine/machine_esp32s3.go"
echo "patched: $CORES_DST (cores runtime)"

# --- cores: ASM cpu1 entry ---
python3 - "$ROOT/src/device/esp/esp32s3.S" "$HERE/call_start_cpu1.S.inc" <<'PY'
import sys
from pathlib import Path
asm = Path(sys.argv[1])
inc = Path(sys.argv[2]).read_text()
text = asm.read_text()
if "call_start_cpu1:" in text:
    print(f"already patched: {asm} (cpu1)")
    raise SystemExit(0)
marker = "// tinygo_scanCurrentStack — Spill all Xtensa register windows"
idx = text.find(marker)
if idx < 0:
    # alternate marker
    idx = text.find(".global tinygo_scanCurrentStack")
    if idx < 0:
        sys.exit("marker not found in esp32s3.S")
    # back up to section comment
    sec = text.rfind("// -----", 0, idx)
    if sec >= 0:
        idx = sec
bak = asm.with_suffix(asm.suffix + ".bak-cores")
if not bak.exists():
    bak.write_text(text)
    print(f"backup: {bak}")
# insert before scanCurrentStack block — find preceding separator
# Prefer inserting after "1:  j 1b\n\n" that follows call_start_cpu0 main return
needle = "    // If main returns, loop forever.\n1:  j 1b\n\n"
pos = text.find(needle)
if pos < 0:
    sys.exit("cpu0 main-return loop not found in esp32s3.S")
insert_at = pos + len(needle)
new = text[:insert_at] + inc.rstrip() + "\n\n" + text[insert_at:]
asm.write_text(new)
print(f"patched: {asm} (cpu1 entry)")
PY

# --- cores: task stack build tag + per-core systemStack ---
python3 - "$ROOT/src/internal/task/task_stack_esp32.go" <<'PY'
import sys
from pathlib import Path
p = Path(sys.argv[1])
text = p.read_text()
if "runtime_systemStackPtr" in text:
    print(f"already patched: {p} (task stack)")
    raise SystemExit(0)
bak = p.with_suffix(p.suffix + ".bak-cores")
if not bak.exists():
    bak.write_text(text)
    print(f"backup: {bak}")
text = text.replace(
    "//go:build scheduler.tasks && (esp32 || esp32s3)",
    "//go:build (scheduler.tasks || scheduler.cores) && (esp32 || esp32s3)",
    1,
)
if 'import (\n\t"unsafe"\n)' in text:
    text = text.replace(
        'import (\n\t"unsafe"\n)',
        'import (\n\t_ "unsafe"\n\t"unsafe"\n)',
        1,
    )
text = text.replace(
    "var systemStack uintptr\n",
    "//go:linkname runtime_systemStackPtr runtime.systemStackPtr\n"
    "func runtime_systemStackPtr() *uintptr\n\n",
    1,
)
text = text.replace("swapTask(s.sp, &systemStack)", "swapTask(s.sp, runtime_systemStackPtr())")
text = text.replace(
    """func (s *state) pause() {
	newStack := systemStack
	systemStack = 0
	swapTask(newStack, &s.sp)
}""",
    """func (s *state) pause() {
	systemStackPtr := runtime_systemStackPtr()
	newStack := *systemStackPtr
	*systemStackPtr = 0
	swapTask(newStack, &s.sp)
}""",
)
text = text.replace(
    """func SystemStack() uintptr {
	return systemStack
}""",
    """func SystemStack() uintptr {
	return *runtime_systemStackPtr()
}""",
)
if "var systemStack" in text or "return systemStack" in text:
    sys.exit("task_stack_esp32.go patch incomplete")
p.write_text(text)
print(f"patched: {p} (task stack)")
PY

# --- cores: linker stack1 + ets_set_appcpu_boot_addr ---
python3 - "$ROOT/targets/esp32s3.ld" <<'PY'
import sys
from pathlib import Path
p = Path(sys.argv[1])
text = p.read_text()
changed = False
bak = p.with_suffix(p.suffix + ".bak-cores")
if not bak.exists():
    bak.write_text(text)
    print(f"backup: {bak}")

if "_stack1_top" not in text:
    old = """        . = ALIGN(16);
        . += _stack_size;
        _stack_top = .;
    } >DRAM"""
    new = """        . = ALIGN(16);
        . += _stack_size;
        _stack_top = .;
        . = ALIGN(16);
        . += _stack_size;
        _stack1_top = .;
    } >DRAM"""
    if old not in text:
        sys.exit("stack section marker not found in esp32s3.ld")
    text = text.replace(old, new, 1)
    changed = True

if "ets_set_appcpu_boot_addr" not in text:
    old = "memcmp = 0x4000120c;\n"
    new = "memcmp = 0x4000120c;\nets_set_appcpu_boot_addr = 0x40000720;\n"
    if old not in text:
        sys.exit("memcmp ROM marker not found in esp32s3.ld")
    text = text.replace(old, new, 1)
    changed = True

p.write_text(text)
print(f"patched: {p} (ld stack1)" if changed else f"already patched: {p} (ld)")
PY

echo "tinygo cores+uart patches applied to $ROOT"
