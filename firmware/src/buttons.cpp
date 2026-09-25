#include "buttons.h"
#include "pins.h"

volatile uint8_t g_btn_pend_next = 0;
volatile uint8_t g_btn_pend_ok = 0;

static void IRAM_ATTR onNextISR() { g_btn_pend_next = 1; }
static void IRAM_ATTR onOkISR() { g_btn_pend_ok = 1; }

void Buttons::begin() {
  pinMode(PIN_BTN_NEXT, INPUT_PULLUP);
  pinMode(PIN_BTN_OK, INPUT_PULLUP);
  last_next_ = digitalRead(PIN_BTN_NEXT);
  last_ok_ = digitalRead(PIN_BTN_OK);
  attachInterrupt(digitalPinToInterrupt(PIN_BTN_NEXT), onNextISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_BTN_OK), onOkISR, FALLING);
}

bool Buttons::pending() const {
  if (g_btn_pend_next || g_btn_pend_ok) return true;
  return digitalRead(PIN_BTN_NEXT) == LOW || digitalRead(PIN_BTN_OK) == LOW;
}

bool Buttons::nextDown() const { return digitalRead(PIN_BTN_NEXT) == LOW; }
bool Buttons::okDown() const { return digitalRead(PIN_BTN_OK) == LOW; }

void Buttons::poll(bool *pressed_next, bool *pressed_ok) {
  *pressed_next = false;
  *pressed_ok = false;
  uint32_t now = millis();

  if (g_btn_pend_next) {
    g_btn_pend_next = 0;
    if (now - next_at_ > debounce_ms_) {
      *pressed_next = true;
      next_at_ = now;
      last_next_ = false;
    }
  }
  if (g_btn_pend_ok) {
    g_btn_pend_ok = 0;
    if (now - ok_at_ > debounce_ms_) {
      *pressed_ok = true;
      ok_at_ = now;
      last_ok_ = false;
    }
  }

  bool n = digitalRead(PIN_BTN_NEXT);
  bool o = digitalRead(PIN_BTN_OK);
  if (!*pressed_next && !n && last_next_ && now - next_at_ > debounce_ms_) {
    *pressed_next = true;
    next_at_ = now;
  }
  if (!*pressed_ok && !o && last_ok_ && now - ok_at_ > debounce_ms_) {
    *pressed_ok = true;
    ok_at_ = now;
  }
  last_next_ = n;
  last_ok_ = o;
}
