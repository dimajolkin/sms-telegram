#!/usr/bin/env bash
# Apply ESP32-S3 UART fix to a TinyGo tree (default: brew TinyGo).
set -euo pipefail
ROOT="${1:-$(tinygo env TINYGOROOT)}"
TARGET="$ROOT/src/machine/machine_esp32s3.go"
BLOCK="$(cd "$(dirname "$0")" && pwd)/uart_block.go.inc"

if [[ ! -f "$TARGET" ]]; then
  echo "not found: $TARGET" >&2
  exit 1
fi
if [[ ! -f "$BLOCK" ]]; then
  echo "not found: $BLOCK" >&2
  exit 1
fi

python3 - "$TARGET" "$BLOCK" <<'PY'
import sys
from pathlib import Path

target = Path(sys.argv[1])
block = Path(sys.argv[2]).read_text()
text = target.read_text()

start = text.find("var DefaultUART = UART0")
if start < 0:
    sys.exit("marker not found: var DefaultUART = UART0")
# end after flush() of UART stub (before GetRNG)
end = text.find("\n// GetRNG returns", start)
if end < 0:
    end = text.find("\nfunc GetRNG", start)
if end < 0:
    sys.exit("marker not found: GetRNG after UART block")

# backup once
bak = target.with_suffix(target.suffix + ".bak-uart")
if not bak.exists():
    bak.write_text(text)
    print(f"backup: {bak}")

new = text[:start] + block.rstrip() + "\n\n" + text[end+1:]
target.write_text(new)
print(f"patched: {target}")
PY
