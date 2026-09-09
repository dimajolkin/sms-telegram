# espradio ESP32-S3: WiFi CPU int 12 → 18

TinyGo `interrupt.New` numbers are fixed at compile time. Stock `espradio` and
our UART patch both used **CPU int 12** → linker conflict.

| Peripheral | CPU int |
|------------|---------|
| UART RX (machine) | **12** (proven) |
| WiFi MAC (espradio) | **18** (this patch) |
| BT | 13, 17 |

```bash
./patches/espradio/apply.sh
./patches/tinygo/apply.sh   # UART on 12
```

WiFi blob arena остаётся **48 KB** (32 KB на практике ломал `NetConnect`).

After `go clean -modcache` / upgrade espradio — re-run `apply.sh`.
