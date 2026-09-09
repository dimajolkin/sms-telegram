#!/usr/bin/env bash
# espradio ESP32-S3: WiFi CPU interrupt 12 → 18 (UART keeps proven int 12).
set -euo pipefail
MOD="$(go env GOPATH)/pkg/mod/tinygo.org/x/espradio@v0.3.0"
GO="$MOD/radio_esp32s3.go"
C="$MOD/esp32s3/isr.c"

if [[ ! -f "$GO" || ! -f "$C" ]]; then
  echo "espradio v0.3.0 not found under $MOD — run go mod download" >&2
  exit 1
fi

chmod -R u+w "$MOD" 2>/dev/null || true

python3 - "$GO" "$C" <<'PY'
from pathlib import Path
import sys

go, c = Path(sys.argv[1]), Path(sys.argv[2])

def patch(path: Path, old: str, new: str, tag: str):
    text = path.read_text()
    if new in text and old not in text:
        print(f"already patched: {path} ({tag})")
        return
    if old not in text:
        sys.exit(f"marker not found in {path}: {old!r}")
    bak = path.parent / (path.name + ".bak-wifi-int")
    if not bak.exists():
        bak.write_text(text)
        print(f"backup: {bak}")
    path.write_text(text.replace(old, new, 1))
    print(f"patched: {path} ({tag})")

patch(go, "const wifiCPUInterrupt = 12", "const wifiCPUInterrupt = 18", "wifi IRQ")
patch(c, "#define ESPRADIO_WIFI_CPU_INT  12u", "#define ESPRADIO_WIFI_CPU_INT  18u", "wifi IRQ C")
PY
