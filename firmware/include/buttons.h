#pragma once

#include <Arduino.h>

class Buttons {
 public:
  void begin();
  bool pending() const;
  void poll(bool *pressed_next, bool *pressed_ok);
  bool nextDown() const;
  bool okDown() const;

 private:
  bool last_next_ = true;
  bool last_ok_ = true;
  uint32_t next_at_ = 0;
  uint32_t ok_at_ = 0;
  uint32_t debounce_ms_ = 40;
};
