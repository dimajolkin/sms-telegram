# AGENT_PLAN — SMS → Telegram (ESP only)

Notion: **SMS → Telegram** (Hobby).

## Архитектура

Seeed XIAO ESP32S3 + SIM800L + ST7789V 1.14. TinyGo + espradio WiFi → Telegram. Локальный UI на TFT.

## Команды бота

- `/start`, `/sms`, `/ussd`, `/help`

## UI (кнопки)

- Next (GPIO0): Status→Inbox / следующий SMS
- OK (GPIO1): refresh / открыть / назад

## Очередь

1. `make flash` при подключённом USB
2. Проверить Status bars + Inbox
3. Реальный SMS / USSD
