#pragma once

#include <stdint.h>

enum TgEventType : uint8_t {
  TG_EVT_SMS = 0,
  TG_EVT_MISSED,
  TG_EVT_TEXT,
};

struct TgEvent {
  TgEventType type;
  int sms_index;  // для TG_EVT_SMS — удалить после отправки; -1 иначе
  char from[32];
  char text[480];
};

enum ModemCmdType : uint8_t {
  MODEM_CMD_SEND_SMS = 0,
  MODEM_CMD_USSD,
  MODEM_CMD_LIST_MISSED,
  MODEM_CMD_REFRESH_STATUS,
};

struct ModemCmd {
  ModemCmdType type;
  char arg1[64];   // номер / USSD-код
  char arg2[320];  // текст SMS
};
