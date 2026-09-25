#pragma once

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "modem.h"

struct StatusView {
  bool wifi_ok;
  int bars;
  const char *oper;
  int queue;   // очередь в Telegram
  int missed;
  const char *clock;
  int alive;
  const char *ip;
  const char *hint_l;
  const char *hint_r;
};

class Display {
 public:
  bool begin();
  void clear();
  void sleep();
  void wake();
  void drawBoot(const char *msg);
  void drawStatus(const StatusView &v);
  void drawInbox(const SMS *list, int n, int idx);
  void drawSMSDetail(const SMS &s);

  int16_t width() const { return w_; }
  int16_t height() const { return h_; }

 private:
  Adafruit_ST7789 *tft_ = nullptr;
  int16_t w_ = 240;
  int16_t h_ = 135;

  void line(int16_t x, int16_t y, const char *s, uint16_t c);
  void drawSoftkeys(const char *left, const char *right);
  void drawAntenna(int16_t x, int16_t y, int bars);
  void drawAlive(int16_t x, int16_t y, int frame);
};
