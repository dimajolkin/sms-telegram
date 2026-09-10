# TinyGo ESP32-S3 UART + cores scheduler patches

## UART

Полный UART Configure + RX IRQ (CPU int **12**). Блок: `uart_block.go.inc`.

## Cores (2-е ядро)

Порт [tinygo#5353](https://github.com/tinygo-org/tinygo/issues/5353) / [PR #5358](https://github.com/tinygo-org/tinygo/pull/5358) под TinyGo **0.42**:

| Файл | Назначение |
|------|------------|
| `runtime_esp32s3_cores.go` | `numCPU=2`, старт APP CPU, crosscore IRQ **14** |
| `call_start_cpu1.S.inc` | entry `call_start_cpu1` |
| `apply.sh` | UART + cores в TinyGo tree |

Сборка (опционально, **WiFi пока падает** — InstTLBMiss в #5358):

```bash
SCHEDULER=cores make flash
```

По умолчанию — одно ядро (`tasks`). После boot при cores: `NumCPU 2`.
