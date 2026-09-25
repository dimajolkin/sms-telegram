#include "ui.h"
#include "config.h"
#include "pins.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

void UI::begin(Display *disp, Buttons *btns, Modem *modem,
               SharedStatus *status) {
  disp_ = disp;
  btns_ = btns;
  modem_ = modem;
  status_ = status;
  last_active_ms_ = millis();
  dirty_ = true;
}

void UI::wake() {
  last_active_ms_ = millis();
  last_clock_sec_ = -1;
  if (!asleep_) return;
  asleep_ = false;
  disp_->wake();
  dirty_ = true;
}

void UI::sleepScreen() {
  if (asleep_) return;
  asleep_ = true;
  disp_->sleep();
  dirty_ = false;
}

void UI::refreshStatus() {
  int csq = 99, bars = 0;
  modem_->signalQuality(&csq, &bars);
  bars_ = bars;
  modem_->operatorName(oper_, sizeof(oper_));
  sms_count_ = modem_->smsCount();
  missed_count_ = modem_->missedCount();
  status_update_modem(status_, bars_, oper_, sms_count_, missed_count_);
  last_poll_ms_ = millis();
  if (!asleep_) dirty_ = true;
}

void UI::loadInbox() {
  inbox_n_ = modem_->listSMS(inbox_, 8);
  if (inbox_n_ < 0) inbox_n_ = 0;
  inbox_idx_ = inbox_n_ > 0 ? inbox_n_ - 1 : 0;
  dirty_ = true;
}

void UI::formatClock(char *out, size_t out_len, bool blink_colon) {
  time_t now = time(nullptr);
  struct tm tm;
  localtime_r(&now, &tm);
  if (now < 1700000000) {
    snprintf(out, out_len, "--:--");
    return;
  }
  if (blink_colon)
    snprintf(out, out_len, "%02d %02d", tm.tm_hour, tm.tm_min);
  else
    snprintf(out, out_len, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

void UI::paint(uint32_t now_ms) {
  (void)now_ms;
  switch (screen_) {
    case SCREEN_STATUS: {
      char clk[8];
      time_t t = time(nullptr);
      struct tm tm;
      localtime_r(&t, &tm);
      formatClock(clk, sizeof(clk), (tm.tm_sec % 2) == 1);
      bool wifi = false;
      int bars = bars_, sms = sms_count_, missed = missed_count_, queue = 0;
      char op[24];
      char ip[16];
      strncpy(op, oper_, sizeof(op));
      ip[0] = 0;
      status_snapshot(status_, &wifi, &bars, op, sizeof(op), &sms, &missed,
                      &queue, ip, sizeof(ip));
      wifi_ok_ = wifi;
      StatusView v{wifi_ok_, bars,    op,     queue, missed, clk,
                   alive_,   ip,      "Inbox", "Refresh"};
      disp_->drawStatus(v);
      break;
    }
    case SCREEN_INBOX:
      disp_->drawInbox(inbox_, inbox_n_, inbox_idx_);
      break;
    case SCREEN_DETAIL:
      if (inbox_n_ > 0 && inbox_idx_ < inbox_n_)
        disp_->drawSMSDetail(inbox_[inbox_idx_]);
      break;
  }
}

void UI::tick() {
  bool n = false, o = false;
  btns_->poll(&n, &o);
  uint32_t now = millis();

  if (asleep_) {
    if (n || o) {
      wake();
      paint(now);
      dirty_ = false;
      return;
    }
    if (screen_ == SCREEN_STATUS && now - last_poll_ms_ > STATUS_REFRESH_MS)
      refreshStatus();
    return;
  }

  if (n || o)
    last_active_ms_ = now;
  else if (now - last_active_ms_ > SCREEN_TIMEOUT_MS) {
    sleepScreen();
    return;
  }

  if (screen_ == SCREEN_STATUS && !asleep_) {
    time_t t = time(nullptr);
    struct tm tm;
    localtime_r(&t, &tm);
    int sec = tm.tm_sec;
    bool held = btns_->nextDown() || btns_->okDown();
    if (sec != last_clock_sec_ || held) {
      last_clock_sec_ = sec;
      alive_ = (alive_ + 1) & 3;
      dirty_ = true;
    }
    int q = status_tg_queue_get(status_);
    if (q != last_queue_) {
      last_queue_ = q;
      dirty_ = true;
    }
  }

  switch (screen_) {
    case SCREEN_STATUS:
      if (n) {
        screen_ = SCREEN_INBOX;
        paint(now);
        loadInbox();
        paint(now);
        dirty_ = false;
        return;
      }
      if (o) {
        paint(now);
        refreshStatus();
        paint(now);
        dirty_ = false;
        return;
      }
      if (now - last_poll_ms_ > STATUS_REFRESH_MS) refreshStatus();
      break;
    case SCREEN_INBOX:
      // Next: листать; после последнего — Status. OK: сразу Status.
      if (n) {
        if (inbox_n_ <= 0) {
          screen_ = SCREEN_STATUS;
          dirty_ = true;
        } else {
          inbox_idx_++;
          if (inbox_idx_ >= inbox_n_) {
            inbox_idx_ = 0;
            screen_ = SCREEN_STATUS;
          }
          dirty_ = true;
        }
      }
      if (o) {
        screen_ = SCREEN_STATUS;
        dirty_ = true;
      }
      break;
    case SCREEN_DETAIL:
      // любая кнопка → Inbox; повторный Back с Inbox → Status
      if (o || n) {
        screen_ = SCREEN_INBOX;
        dirty_ = true;
      }
      break;
  }

  if (!dirty_) return;
  dirty_ = false;
  paint(now);
}
