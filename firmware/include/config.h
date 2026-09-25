#pragma once

#include <stdint.h>
#include <time.h>

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif
#ifndef TELEGRAM_BOT_TOKEN
#define TELEGRAM_BOT_TOKEN ""
#endif
#ifndef TELEGRAM_CHAT_ID
#define TELEGRAM_CHAT_ID ""
#endif

static const uint32_t UART_BAUD = 9600;
static const uint32_t SMS_POLL_MS = 3000;
static const uint32_t TG_POLL_MS = 1200;
static const uint32_t SCREEN_TIMEOUT_MS = 30000;
static const uint32_t STATUS_REFRESH_MS = 15000;
static const uint32_t MISSED_SILENCE_MS = 6000;

// Еженедельная проверка баланса (*100#)
static const time_t BALANCE_CHECK_INTERVAL_SEC = 7L * 24 * 3600;
static const float BALANCE_WARN_BELOW = 100.0f;
static const uint32_t BALANCE_FIRST_CHECK_AFTER_MS = 120000;  // 2 мин после boot
static const uint32_t BALANCE_POLL_MS = 60000;                // как часто смотреть таймер

static const int PANEL_W = 135;
static const int PANEL_H = 240;
