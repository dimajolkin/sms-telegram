# SMS → Telegram (Seeed XIAO ESP32S3)

**Seeed XIAO ESP32S3** + **SIM800L** + **ST7789V 1.14″**. Software TLS на устройстве.

```bash
cp secrets.mk.example secrets.mk
make flash PORT=/dev/cu.usbmodem*
```

## Распиновка XIAO

| Функция | XIAO | GPIO |
|---------|------|------|
| Modem RX ← MCU TX | **D6** | 43 (U0TXD) |
| Modem TX → MCU RX | **D7** | 44 (U0RXD) |
| Modem RST | **D2** | 3 |
| TFT SCL | D8 | 7 |
| TFT SDA (MOSI) | D10 | 9 |
| TFT CS | D9 | 8 |
| TFT DC | D4 | 5 |
| TFT RES | D5 | 6 |
| TFT BLK | D3 | 4 |
| TFT VCC / GND | 3V3 / GND | |
| Button Next | D0 → GND | 1 |
| Button OK | D1 → GND | 2 |

UART модема **скрестить** под UART0 IO_MUX. Сначала:
`./patches/tinygo/apply.sh` и `./patches/espradio/apply.sh` (UART IRQ=12, WiFi IRQ=18).  
`make flash` может падать на MD5 — тогда `esptool write-flash 0x0 fw.bin`.

## Команды

`/balance`, `/missed`, `/sms +7900… текст`, `/ussd *100#`, `/help`.

Список команд — в `firmware/commands.go` (меню Telegram + `/help`).
