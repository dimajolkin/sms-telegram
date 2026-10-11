#pragma once

#include "pdu.h"

#include <stdint.h>

enum TgEventType : uint8_t {
  TG_EVT_SMS = 0,
  TG_EVT_MISSED,
  TG_EVT_TEXT,
};

struct TgEvent {
  TgEventType type;
  int sms_index;
  int sms_indices[SMS_PARTS_MAX];
  uint8_t sms_n;
  char from[32];
  char text[SMS_TEXT_MAX];
};

enum ModemCmdType : uint8_t {
  MODEM_CMD_SEND_SMS = 0,
  MODEM_CMD_USSD,
  MODEM_CMD_LIST_MISSED,
  MODEM_CMD_REFRESH_STATUS,
  MODEM_CMD_FETCH_SMS,
};

struct ModemCmd {
  ModemCmdType type;
  char arg1[64];   // номер / USSD-код
  char arg2[320];  // текст SMS
};
