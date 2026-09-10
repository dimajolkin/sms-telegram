# SMS ↔ Telegram gateway (TinyGo)

A small **TinyGo** firmware for **Seeed XIAO ESP32-S3** that bridges a **SIM800L** cellular modem and **Telegram Bot API** over Wi‑Fi, with a Nokia-style **ST7789** status screen.

Use it as a practical example of:

- TinyGo on ESP32-S3 (UART, SPI display, GPIO buttons)
- Software TLS + HTTPS to `api.telegram.org` on a constrained MCU
- AT-command modem control (SMS, USSD, CLIP / missed calls)
- Long-polling Telegram bots without a cloud relay

```text
  Phone / SMS ──► SIM800L ──UART──► XIAO ESP32-S3 ──Wi‑Fi TLS──► Telegram
                      ▲                    │
                      │                    └── ST7789 status UI
                 USSD / CLIP
```

## Features

| Feature | Behavior |
|---------|----------|
| Incoming SMS | Forwarded to your chat (UCS-2 decoded) |
| Missed calls | `AT+CLIP` → Telegram after ring silence |
| Outgoing SMS | `/sms +7900… text` |
| USSD / balance | `/balance` (`*100#`), `/ussd *code#` |
| Missed list | `/missed` (modem MC/RC phonebook) |
| Display | Signal, Wi‑Fi, clock, SMS/CALLS counts, softkeys |
| Buttons | D0 = Next / Inbox, D1 = OK / Refresh (to GND) |

Bot commands are defined once in [`firmware/commands.go`](firmware/commands.go) and registered with `setMyCommands`.

## Hardware

- [Seeed XIAO ESP32-S3](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/)
- SIM800L (or compatible) + SIM with SMS
- ST7789V 1.14″ IPS (135×240)
- Two momentary buttons to GND

### Pinout (XIAO)

| Function | XIAO | GPIO |
|----------|------|------|
| Modem RX ← MCU TX | **D6** | 43 (U0TXD) |
| Modem TX → MCU RX | **D7** | 44 (U0RXD) |
| Modem RST (idle HIGH) | **D2** | 3 |
| TFT SCL | D8 | 7 |
| TFT SDA (MOSI) | D10 | 9 |
| TFT CS | D9 | 8 |
| TFT DC | D4 | 5 |
| TFT RES | D5 | 6 |
| TFT BLK | D3 | 4 |
| TFT VCC / GND | 3V3 / GND | |
| Button Next | D0 → GND | 1 |
| Button OK | D1 → GND | 2 |

Cross TX/RX for the modem (UART0 IO_MUX pins).

## Requirements

- [TinyGo](https://tinygo.org/) **0.42+** (`xiao-esp32s3` target)
- `esptool` (for flash if `tinygo flash` MD5-check fails)
- Wi‑Fi credentials + a Telegram bot token ([@BotFather](https://t.me/BotFather))

## Quick start

```bash
# 1) Apply required TinyGo / espradio IRQ patches (once per TinyGo install)
./patches/tinygo/apply.sh      # full UART + optional dual-core bits
./patches/espradio/apply.sh    # Wi‑Fi CPU IRQ 12 → 18 (UART keeps 12)

# 2) Secrets (not committed)
cp secrets.mk.example secrets.mk
# edit: WIFI_SSID, WIFI_PASSWORD, TELEGRAM_BOT_TOKEN, TELEGRAM_CHAT_ID

# 3) Build & flash
make flash PORT=/dev/cu.usbmodem*
```

If `make flash` fails on checksum, build then flash manually:

```bash
make firmware
esptool --chip esp32s3 -p /dev/cu.usbmodem* write-flash 0x0 bin/firmware.elf
# or use the .bin produced by your usual tinygo build -o bin/firmware.bin …
```

Open the bot in Telegram, send `/start` or `/help`. Incoming SMS and missed calls appear in the chat automatically.

## Telegram bot API usage (TinyGo)

The firmware talks HTTPS directly with a small software TLS stack under [`firmware/swtls`](firmware/swtls) and a thin client in [`firmware/telegram.go`](firmware/telegram.go):

1. **`getMe`** — connectivity check at boot  
2. **`setMyCommands`** — populate the bot menu from `botCommands`  
3. **`getUpdates`** — long-poll (timeout 0 in loop; handled in firmware main)  
4. **`sendMessage`** — SMS / missed-call / command replies  

Secrets are injected at link time (no plaintext in the repo):

```makefile
-ldflags="-X main.ssid=… -X main.password=… -X main.botToken=… -X main.chatID=…"
```

Heap is tight: close idle TLS after each request, keep stack around **12 KB**, and avoid large JSON buffers.

### Example commands

```text
/help
/balance
/missed
/sms +79001112233 hello from TinyGo
/ussd *100#
```

## Project layout

```text
firmware/          main TinyGo app (modem, UI, Telegram, swtls)
patches/tinygo/    ESP32-S3 UART (+ experimental cores) for TinyGo 0.42
patches/espradio/  move Wi‑Fi IRQ off UART’s CPU interrupt
uart-test/         minimal UART↔modem bring-up
secrets.mk.example template for Wi‑Fi / bot secrets
```

## Notes & limits

- **Boot order matters:** display → Wi‑Fi/NTP → UART/modem → Telegram. Configuring UART before `espradio` can clear `INTENABLE` and kill RX.
- **IRQ map:** GPIO≈10, UART=**12**, Wi‑Fi=**18** (after patches). Dual-core (`SCHEDULER=cores`) is experimental and currently unstable with Wi‑Fi ([tinygo#5358](https://github.com/tinygo-org/tinygo/issues/5353)).
- Screen sleeps after 30 s idle; wake on button or SMS / missed call.
- This is a hobby gateway — not a hardened SMS appliance.

## License

Use and modify as you like for personal / educational projects. Upstream TinyGo and driver licenses still apply.
