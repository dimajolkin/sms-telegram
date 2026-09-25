# AGENT_PLAN — SMS → Telegram (PlatformIO)

Notion: **SMS → Telegram** (Hobby).

## Архитектура

Seeed XIAO ESP32S3 + SIM800L + ST7789V 1.14.
PlatformIO Arduino + FreeRTOS: Core0 WiFi/Telegram, Core1 modem/UI.

## Команды бота

- `/start`, `/sms`, `/ussd`, `/balance`, `/missed`, `/help`

## UI (кнопки)

- Next (D0): Status→Inbox / следующий SMS
- OK (D1): refresh / Back на Status

## Очередь

1. `make flash` при подключённом USB
2. Проверить Status (QUEUE, IP) + Inbox Back
3. Реальный SMS / USSD
