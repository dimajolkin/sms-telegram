#include "display.h"
#include "pins.h"
#include "config.h"
#include <SPI.h>
#include <string.h>

// Nokia-ish phosphor (RGB565)
static const uint16_t COL_BG = 0x08E2;      // ~12,28,14
static const uint16_t COL_FG = 0xA6F7;      // ~170,220,120
static const uint16_t COL_MUTED = 0x4567;   // ~70,110,60
static const uint16_t COL_OK = 0xBEF9;      // ~190,240,140
static const uint16_t COL_WARN = 0xC5A7;    // ~200,180,60
static const uint16_t COL_ACCENT = 0x9692;  // ~150,210,100
static const uint16_t COL_BAR_ON = 0xB6F8;
static const uint16_t COL_BAR_OFF = 0x22A6;
static const uint16_t COL_SOFT = 0x2A28;

bool Display::begin() {
  SPI.begin(PIN_TFT_SCK, -1, PIN_TFT_MOSI);
  pinMode(PIN_TFT_BL, OUTPUT);
  digitalWrite(PIN_TFT_BL, HIGH);

  tft_ = new Adafruit_ST7789(&SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
  tft_->init(PANEL_W, PANEL_H);
  tft_->setRotation(3);  // landscape 240×135, 180° vs rotation 1
  w_ = tft_->width();
  h_ = tft_->height();
  tft_->fillScreen(COL_BG);
  tft_->setTextWrap(false);
  return true;
}

void Display::clear() { tft_->fillScreen(COL_BG); }

void Display::sleep() { digitalWrite(PIN_TFT_BL, LOW); }

void Display::wake() { digitalWrite(PIN_TFT_BL, HIGH); }

void Display::line(int16_t x, int16_t y, const char *s, uint16_t c) {
  tft_->setCursor(x, y);
  tft_->setTextColor(c, COL_BG);
  tft_->setTextSize(1);
  tft_->print(s);
}

void Display::drawSoftkeys(const char *left, const char *right) {
  // setRotation(3): FB-left = physical right (OK/D1), FB-right = physical left (Next/D0)
  const char *fb_left = right;
  const char *fb_right = left;

  int16_t y = h_ - 18;
  tft_->fillRect(0, y - 4, w_, 22, COL_SOFT);
  line(8, y + 4, fb_left, COL_FG);
  int16_t rx = w_ - (int16_t)(6 * strlen(fb_right)) - 8;
  if (rx < w_ / 2) rx = w_ / 2 + 10;
  line(rx, y + 4, fb_right, COL_FG);
}

void Display::drawAntenna(int16_t x, int16_t y, int bars) {
  tft_->fillRect(x + 2, y + 2, 3, 14, COL_BAR_ON);
  for (int16_t i = 0; i < 4; i++) {
    int16_t hh = 4 + i * 3;
    uint16_t c = (i < bars) ? COL_BAR_ON : COL_BAR_OFF;
    tft_->fillRect(x + 8 + i * 9, y + (16 - hh), 6, hh, c);
  }
}

void Display::drawAlive(int16_t x, int16_t y, int frame) {
  uint16_t c = (frame % 2 == 0) ? COL_OK : COL_MUTED;
  tft_->fillRect(x, y, 10, 10, c);
  tft_->fillRect(x + 2, y + 2, 6, 6, COL_BG);
  if (frame % 2 == 0) tft_->fillRect(x + 3, y + 3, 4, 4, c);
}

void Display::drawBoot(const char *msg) {
  clear();
  line(8, 12, "NOKIA-ish", COL_ACCENT);
  line(8, 36, "SMS GATEWAY", COL_FG);
  line(8, 64, msg && msg[0] ? msg : "…", COL_MUTED);
  drawSoftkeys(" ", " ");
}

void Display::drawStatus(const StatusView &v) {
  clear();
  drawAntenna(6, 4, v.bars);
  line(70, 10, v.wifi_ok ? "WIFI+" : "WIFI-", v.wifi_ok ? COL_OK : COL_WARN);

  const char *clk = v.clock && v.clock[0] ? v.clock : "--:--";
  int16_t clk_x = w_ - 18 - (int16_t)(6 * strlen(clk)) - 6;
  line(clk_x, 8, clk, COL_FG);
  drawAlive(w_ - 18, 6, v.alive);

  char op[24];
  strncpy(op, v.oper && v.oper[0] ? v.oper : "—", sizeof(op) - 1);
  op[sizeof(op) - 1] = 0;
  if (strlen(op) > 22) op[22] = 0;
  line(8, 28, op, COL_MUTED);

  line(8, 44, "QUEUE", COL_MUTED);
  line(120, 44, "CALLS", COL_MUTED);

  char qbuf[8], miss[8];
  snprintf(qbuf, sizeof(qbuf), "%d", v.queue < 0 ? 0 : v.queue);
  if (v.missed >= 0)
    snprintf(miss, sizeof(miss), "%d", v.missed);
  else
    strcpy(miss, "?");

  tft_->setTextSize(2);
  tft_->setTextColor(COL_FG, COL_BG);
  tft_->setCursor(8, 60);
  tft_->print(qbuf);
  tft_->setCursor(120, 60);
  tft_->print(miss);
  tft_->setTextSize(1);

  // IP над softkeys (низ экрана)
  const char *ip = v.ip && v.ip[0] ? v.ip : "—.—.—.—";
  line(8, h_ - 30, ip, COL_MUTED);

  drawSoftkeys(v.hint_l && v.hint_l[0] ? v.hint_l : "Inbox",
               v.hint_r && v.hint_r[0] ? v.hint_r : "Refresh");
}

void Display::drawInbox(const SMS *list, int n, int idx) {
  clear();
  line(8, 8, "INBOX", COL_ACCENT);
  if (n <= 0) {
    line(8, 40, "(empty)", COL_MUTED);
    drawSoftkeys("Back", "Back");
    return;
  }
  if (idx < 0) idx = 0;
  if (idx >= n) idx = n - 1;
  char cnt[16];
  snprintf(cnt, sizeof(cnt), "%d/%d", idx + 1, n);
  line(90, 8, cnt, COL_MUTED);

  int start = idx;
  if (start > 0 && idx == n - 1) start = idx - 1;
  int16_t y = 28;
  for (int i = start; i < n && y < 100; i++) {
    uint16_t c = (i == idx) ? COL_FG : COL_MUTED;
    char row[40];
    snprintf(row, sizeof(row), "%s%.18s", i == idx ? "> " : "  ", list[i].from);
    line(4, y, row, c);
    y += 12;
    char preview[36];
    strncpy(preview, list[i].text, 28);
    preview[28] = 0;
    line(14, y, preview, COL_MUTED);
    y += 16;
    if (i >= start + 1) break;
  }
  drawSoftkeys("Next", "Back");
}

void Display::drawSMSDetail(const SMS &s) {
  clear();
  char from[32];
  strncpy(from, s.from, 26);
  from[26] = 0;
  line(8, 8, from, COL_ACCENT);

  const char *text = s.text;
  int16_t y = 28;
  while (*text && y < 100) {
    char chunk[36];
    size_t n = 0;
    while (text[n] && text[n] != '\n' && n < 32) n++;
    memcpy(chunk, text, n);
    chunk[n] = 0;
    line(8, y, chunk, COL_FG);
    y += 12;
    text += n;
    if (*text == '\n') text++;
  }
  drawSoftkeys("Back", "Back");
}
