# TinyGo ESP32-S3 UART patch

## В чём была проблема

1. **Stub** TinyGo 0.42: `Configure` только `CLKDIV` — нет clock/пинов/RX IRQ.
2. **GPIO matrix** (UART1) у нас не выводил TX на пад; **UART0 + IO_MUX** на GPIO43/44 — ок.
3. **IRQ:** int 11 = profiling. UART → **CPU int 12**. WiFi espradio сдвинут на **18** (`patches/espradio`). USB=8, timer=9, GPIO=10, BT=13/17.

Рабочая схема XIAO: **D6→Modem RX**, **D7←Modem TX**, `DefaultUART` / UART0.

## Файлы

| Файл | Назначение |
|------|------------|
| `uart_block.go.inc` | Замена блока UART в `machine_esp32s3.go` |
| `apply.sh` | Вставляет блок в TinyGo tree |

## Применить

```bash
# Homebrew TinyGo
./patches/tinygo/apply.sh

# или явный путь
./patches/tinygo/apply.sh "$(tinygo env TINYGOROOT)"
```

Бэкап: `machine_esp32s3.go.bak-uart`.

Проверка:

```bash
cd uart-test && make bin
# проводка: D6→ModemRX, D7←ModemTX
esptool.py --chip esp32s3 --port /dev/cu.usbmodem2101 write-flash 0x0 /tmp/uart-test.bin
tinygo monitor -port=/dev/cu.usbmodem2101
# ждать: AT … [AT\r\r\nOK\r\n]
```

После патча обычный API:

```go
uart := machine.DefaultUART // UART0
uart.Configure(machine.UARTConfig{BaudRate: 9600}) // пины board default 43/44
```

## Upstream PR

«esp32s3: implement UART.Configure (clock, IO_MUX/matrix pins, RX IRQ)»  
Опираться на C3 + IO_MUX для native pins; matrix — follow-up если нужно.