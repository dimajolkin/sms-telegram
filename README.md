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
- Bulk capacitor on modem VCC (current peaks)

### Breadboard prototype

Current bring-up on a full-size breadboard (XIAO → SIM800L → ST7789 → softkeys + bulk cap):

![Breadboard prototype](docs/hardware/breadboard-prototype.jpg)

### Enclosure concept — 46 × 78 × 16 mm, two-part, 4× M3 recessed

Handheld body, landscape 1.14″ window (matches the firmware's 240×135 `Rotation90` frame), two softkeys under the screen, XIAO USB-C exits through the top edge for power/flash. Upper shell + flat bottom cover, held by four M3 × 6 button-head screws (ISO 7380) into brass heat-set inserts from the M1.4–M6 kit in the Notion inventory; the heads sit in counterbores, flush with the bottom. M6 was the first pick, but an M6 head (Ø10.5) cannot hide in the corner of a 46 mm body — the pillar centre would have to move 2.5 mm inward and the XIAO no longer fits between the top pillars.

| View | Preview |
|------|---------|
| Exterior | ![Enclosure iso](docs/hardware/enclosure-iso.png) |
| Front | ![Enclosure front](docs/hardware/enclosure-front.png) |
| Packing (shell removed) | ![Enclosure packing](docs/hardware/enclosure-packing.png) |
| Packing, top-down | ![Enclosure packing top](docs/hardware/enclosure-packing-top.png) |
| Cover fixtures | ![Cover fixtures](docs/hardware/enclosure-cover-fixtures.png) |
| Shell inside (window lip, pocket ribs, USB U-notch) | ![Shell inside](docs/hardware/enclosure-shell-inside.png) |
| Bottom (recessed M3 heads) | ![Bottom](docs/hardware/enclosure-bottom-iso.png) |

Fit is checked against the manufacturers' 3D models, not against boxes: `vendor/ER-TFTM1.14-1.step` (EastRising display) and `vendor/XIAO-ESP32S3-v2.step` (Seeed). Both sit in the Fusion assembly, so every support surface below is measured off real geometry.

```text
 y=78 ┌──────[USB-C]──────┐   XIAO ESP32-S3 under the display, USB out the top wall
      │ (M3)          (M3)│   pillars Ø8 at (±17.8, 32/72), insert hole Ø3.4 × 6
      │  ┌─────────────┐  │
      │  │ ST7789 1.14″│  │   PCB 31.4×28 on posts at z 12, screen 24.9×14.9 under the lip
      │  └─────────────┘  │
      │ (M3) (N)  (O) (M3)│   6×6 tact switches (H=7) on a 20×8 strip, flanged caps
      │  ┌────────┐ ┌──┐  │
      │  │SIM800L │ │C │  │   modem 25×23×7 + Ø10×20 bulk cap side by side
      │  └────────┘ └──┘  │   Wi-Fi flex antenna 40×11 on the cover under the display
 y=0  └───────────────────┘
```

#### Display window with a lip (no gap around the glass)

The top plate is 2 mm (z 14–16) and the window is split in two levels: a pocket 32.4 × 18 at z 14–15 that swallows the 32 × 17.6 display glass, and a viewing opening 29.4 × 15.5 at z 15–16. The remaining 1 mm ring overhangs the glass edge by 1.3 mm in X and 1.07 mm in Y, so nothing is visible through the seam, with 0.2 mm of air above the glass for tolerance. The active area (24.9 × 14.9, offset 1.9 mm towards −X on this module) clears the opening by 0.3 mm on every side.

Shell: 2 mm walls, R6 corners, 0.8 mm chamfer on the top edge. Cover: 2 mm plate with four Ø8 bosses rising to z 3.4 under the pillars; each has a Ø3.4 through hole and a Ø6.2 × 1.9 counterbore from below, so the Ø5.7 × 1.65 button head ends 0.25 mm inside the plate and a 1.5 mm floor remains under the pillar. Screw stack: head 0.25–1.9, thread 1.9–7.9, insert 3.4–8.4 (M3 × 5 insert, ≥ 4.5 mm engagement).

Heat-set inserts melt into the **shell** pillars from below: the pilot hole is Ø3.4 × 6 against a Ø4.4 insert, i.e. 0.5 mm of interference per side, and the pillar keeps a 1.8 mm wall around the insert. If your kit sinks too hard, open the hole to Ø4.0–4.2 (`NEW_HOLE_D` in the insert-hole script) — that is the conventional 0.1–0.2 mm per side.

Hole sizes at a glance: Ø3.4 = M3 clearance in the cover **and** the insert pilot hole in the shell, Ø6.2 = head counterbore.

#### Fixtures per node (no glue, everything is captured between shell and cover)

| Node | Shell side | Cover side | Play |
|------|-----------|------------|------|
| Buttons (6×6 tact, 4-pin, from the inventory kit, H = 7 mm) | Ø6.2 holes at (±6.5, 32); caps have a Ø8 × 1 flange under the wall, Ø5.6 stem 0.8 mm proud, Ø3.7 socket for the plunger — a cap cannot fall out once the shell is on | 2 posts 4×4 (z 2–4.3) with Ø1.8 pegs into Ø2 holes in the strip at (±8.5, 32) | 0.1 mm pre-travel |
| Display ER-TFTM1.14-1 (PCB 31.4×28) | L-shaped corner ribs (1.2 mm, z 9.3–14) pocket the module; the 1 mm window lip overlaps the glass edge | 2 posts 3.5×3.5 at (±14, 40.5) and one 8×5.5 post at (0, 50.5–56), all to z 12 — PCB rests with 0 gap | 0.1 mm |
| XIAO ESP32-S3 (PCB 17.8×21) | USB-C U-notch (9.2 wide, open to the shell's bottom edge) so the board slides in with the cover | rails under the textolite (\|x\| 7.7–9.0, z 2–3.5), side stops (\|x\| 9.2–10.4, z→5.5), back stop at y 56 — the same post that carries the display, so plugging the cable pushes the board into it | 0.3 mm side |
| SIM800L | 2 hold-down ribs (y 8–9.2 and 19–20.2, z 9.7–14) | corner stops 1.2 mm (z 2–5) on three sides + a bar at y 25.8–26.6 | 0.2–0.5 mm |
| Bulk cap Ø10 × 20 | finger x 8–18, y 12–14, z 12.7–14 | two U-saddles (R5.2, z 2–7.5) at y 4.5–6.5 and 19.5–21.5 | 0.2 mm |
| Wi-Fi flex | — | adhesive, lies flat at y 42.5–53.5 between the display posts | — |

#### Assembly / disassembly

1. Heat-set four M3 brass inserts into the shell pillars (Ø3.4 hole, 6 mm deep; insert flush with the pillar face).
2. Drop both caps into the shell from inside (flange down). Solder wires to the display (remove the straight header), lay it face-down into the window pocket — the lip catches the glass edge.
3. On the cover: stick the flex antenna, press the button strip onto the pegs (trim switch pins ≤ 2 mm), seat the SIM800L into its stops and the capacitor into the saddles, slide the XIAO onto the rails until its rear edge meets the y 56 stop, USB-C towards the notch.
4. Close the cover — the posts lift the display against the shell, the filler tab closes the U-notch under the connector — and fit four M3 × 6 button-head screws from below; they end up flush. Disassembly is the reverse; no glue anywhere.

#### Files

| File | What |
|------|------|
| [`hardware/enclosure/stl/plate-100x160.stl`](hardware/enclosure/stl/plate-100x160.stl) | Everything in one job for a 100 × 160 mm bed: shell + cover + 2 caps, already oriented (96 × 90 mm footprint) |
| [`hardware/enclosure/stl/enclosure-shell.stl`](hardware/enclosure/stl/enclosure-shell.stl) | Upper shell, print-oriented: front face on the bed, no supports |
| [`hardware/enclosure/stl/enclosure-cover.stl`](hardware/enclosure/stl/enclosure-cover.stl) | Bottom cover, print-oriented: counterbores on the bed |
| [`hardware/enclosure/stl/button-cap-x2.stl`](hardware/enclosure/stl/button-cap-x2.stl) | Button cap, flange on the bed — print two |
| [`hardware/enclosure/sms-telegram-enclosure.step`](hardware/enclosure/sms-telegram-enclosure.step) | Full assembly incl. component placeholders, inserts and screws |
| [`hardware/enclosure/sms-telegram-enclosure.f3d`](hardware/enclosure/sms-telegram-enclosure.f3d) | Fusion 360 source with the parametric timeline |
| [`hardware/enclosure/vendor/`](hardware/enclosure/vendor) | Manufacturer STEP models used for the fit check: EastRising display, Seeed XIAO |
| [`hardware/enclosure/scripts/place_parts.py`](hardware/enclosure/scripts/place_parts.py) | Re-seats both vendor models on their supports in Fusion (positions of inserted components are not stored in the parametric timeline, so any geometry edit drops them back to the origin) |

#### Printing (bed 100 × 160 mm is enough for both parts side by side)

- All STLs are in mm, at the origin, with the part sitting on z = 0 in its print orientation — load and slice, no rotation needed. Suggested: 0.2 mm layers, 3 perimeters, 20 % infill, PETG or PLA. The combined plate is 96 × 90 mm.
- Shell: top face on the bed, no supports — pillars, pocket ribs, SIM ribs and cap finger are vertical walls, the top-edge chamfer replaces the old R1.5 fillet that would have left a 0.75 mm step at layer 2. The USB U-notch has no bridge. The 1 mm window lip prints as the first layers, so the display opening comes out crisp.
- Cover: flat on the bed, all fixtures are 1.2–4 mm vertical features; the Ø1.8 pegs are the smallest detail (0.4 nozzle, ≥ 3 perimeters).
- Caps: flange down, PETG/PLA; 0.2 mm layer keeps the Ø3.7 socket dimension.
- Pillar wall around the insert is 1.8 mm — fine for heat-set M3; the counterbores print as clean 1.9 mm pockets on the bed side.

Open points:

- No screws in the inventory — buy M3 × 6 button head (ISO 7380); M3 × 8 would bottom out in the 6 mm insert hole.
- GSM antenna for the SIM800L is not modelled yet; the Wi-Fi flex is placed under the display, away from the modem.

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
