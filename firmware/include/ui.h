#pragma once

#include <Arduino.h>
#include "buttons.h"
#include "display.h"
#include "modem.h"
#include "status.h"

enum UIScreen : uint8_t { SCREEN_STATUS = 0, SCREEN_INBOX, SCREEN_DETAIL };

class UI {
 public:
  void begin(Display *disp, Buttons *btns, Modem *modem, SharedStatus *status);
  void wake();
  void tick();
  void refreshStatus();

 private:
  Display *disp_ = nullptr;
  Buttons *btns_ = nullptr;
  Modem *modem_ = nullptr;
  SharedStatus *status_ = nullptr;
  UIScreen screen_ = SCREEN_STATUS;

  bool wifi_ok_ = false;
  int bars_ = 0;
  char oper_[24] = {0};
  int sms_count_ = -1;
  int missed_count_ = -1;
  int last_queue_ = -1;
  int alive_ = 0;
  bool dirty_ = true;
  bool asleep_ = false;
  uint32_t last_poll_ms_ = 0;
  uint32_t last_active_ms_ = 0;
  int last_clock_sec_ = -1;

  SMS inbox_[8];
  int inbox_n_ = 0;
  int inbox_idx_ = 0;

  void sleepScreen();
  void loadInbox();
  void paint(uint32_t now_ms);
  void formatClock(char *out, size_t out_len, bool blink_colon);
};
