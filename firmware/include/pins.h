#pragma once

// Seeed XIAO ESP32-S3 — D0…D10

static const int PIN_UART_TX = 43;  // D6 → Modem RX
static const int PIN_UART_RX = 44;  // D7 ← Modem TX

static const int PIN_TFT_SCK = 7;   // D8
static const int PIN_TFT_MOSI = 9;  // D10
static const int PIN_TFT_CS = 8;    // D9
static const int PIN_TFT_DC = 5;    // D4
static const int PIN_TFT_RST = 6;   // D5
static const int PIN_TFT_BL = 4;    // D3

static const int PIN_BTN_NEXT = 1;  // D0 → GND
static const int PIN_BTN_OK = 2;    // D1 → GND
static const int PIN_MODEM_RST = 3; // D2 → SIM800L RST
