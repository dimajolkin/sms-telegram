# SMS ↔ Telegram gateway (PlatformIO / Arduino)

Firmware for **Seeed XIAO ESP32-S3** that bridges a **SIM800L** cellular modem and **Telegram Bot API** over Wi‑Fi, with a Nokia-style **ST7789** status screen.

Uses **native ESP Wi‑Fi + mbedTLS** and **two FreeRTOS cores**. The device is a **gateway**: accept SMS / events and forward to Telegram — not a message store.

```text
  Phone / SMS ──► SIM800L ──UART──► XIAO ESP32-S3 ──Wi‑Fi TLS──► Telegram
                      ▲                    │
                      │                    └── ST7789 status UI
                 USSD / CLIP
```

| Core | Tasks |
|------|--------|
| **0** (PRO) | Wi‑Fi stack, NTP, Telegram HTTPS / long-poll, outbound queue |
| **1** (APP) | SIM800L UART, SMS/CLIP poll, weekly balance check, ST7789 + buttons |

## Features

| Feature | Behavior |
|---------|----------|
| Incoming SMS | Queued → Telegram (UCS-2 decoded), then **deleted from SIM** |
| Missed calls | `AT+CLIP` → Telegram after ring silence |
| Outgoing SMS | `/sms +7900… text` |
| USSD / balance | `/balance` (`*100#`), `/ussd *code#` |
| Weekly balance | Every **7 days** (`*100#`); Telegram warning if balance **&lt; 100** |
| Missed list | `/missed` (modem MC/RC phonebook) |
| Status UI | Signal, Wi‑Fi, clock, **QUEUE** (pending TG sends), CALLS, **IP**, softkeys |
| Inbox | Browse SIM leftovers; **Next** = next / exit after last, **Back** = status |
| Buttons | D0 = Next / Inbox, D1 = OK / Refresh / Back (to GND) |

## Hardware

- [Seeed XIAO ESP32-S3](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/)
- SIM800L (or compatible) + SIM with SMS
- ST7789V 1.14″ IPS (135×240), rotation 180° in firmware
- Two momentary buttons to GND
- Bulk capacitor on modem VCC

Enclosure / STL / Fusion: [`hardware/enclosure/`](hardware/enclosure/). Photos: [`docs/hardware/`](docs/hardware/).

### Pinout (XIAO)

| Function | XIAO | GPIO |
|----------|------|------|
| Modem RX ← MCU TX | **D6** | 43 |
| Modem TX → MCU RX | **D7** | 44 |
| Modem RST (idle HIGH) | **D2** | 3 |
| TFT SCL | D8 | 7 |
| TFT SDA (MOSI) | D10 | 9 |
| TFT CS | D9 | 8 |
| TFT DC | D4 | 5 |
| TFT RES | D5 | 6 |
| TFT BLK | D3 | 4 |
| Button Next | D0 → GND | 1 |
| Button OK | D1 → GND | 2 |

## Requirements

- [PlatformIO](https://platformio.org/) CLI (`pio`)
- Wi‑Fi credentials + Telegram bot token ([@BotFather](https://t.me/BotFather))

## Quick start

```bash
cp firmware/secrets.ini.example firmware/secrets.ini
# edit WIFI_SSID, WIFI_PASSWORD, TELEGRAM_BOT_TOKEN, TELEGRAM_CHAT_ID

make flash          # or: cd firmware && pio run -t upload
make monitor
```

Open the bot, send `/start` or `/help`. Incoming SMS and missed calls appear automatically. **QUEUE** on the screen is the Telegram outbound backlog (decrements after each successful send).

### Example commands

```text
/help
/balance
/missed
/sms +79001112233 hello
/ussd *100#
```

## Project layout

```text
firmware/            PlatformIO project (Arduino + FreeRTOS)
  platformio.ini
  secrets.ini.example
  include/           pins, config, events, headers
  src/               main, modem, telegram, ui, display, …
hardware/enclosure/  case STL / Fusion
```

## Notes

- Boot order: display → Wi‑Fi/NTP → UART/modem → Telegram → pinned tasks.
- Screen sleeps after 30 s idle; wake on button or SMS / missed call.
- Balance check timestamp is stored in NVS (`sms-gw` / `bal_chk`); first run ~2 min after boot, then every 7 days.

## License

Use and modify as you like for personal / educational projects.
