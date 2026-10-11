#pragma once

#include <stddef.h>
#include <stdint.h>

// Длинная SMS: сегменты с UDH (3GPP 23.040), потом одна раскодировка.
static const int SMS_PARTS_MAX = 8;
static const int SMS_TEXT_MAX = 1400;
static const int CONCAT_WAIT_MAX = 4;

struct SMS {
  int index;
  int indices[SMS_PARTS_MAX];
  int index_n;
  char from[32];
  char text[SMS_TEXT_MAX];
};

struct ConcatWait {
  bool used;
  char from[32];
  int ref;
  int total;
  int alphabet;
  uint32_t first_ms;
};

// cmgl — ответ AT+CMGL в PDU-режиме.
// waits — незавершённые склейки между опросами; nullptr, если не нужно.
// skip_idx — индексы SIM, которые уже ушли в Telegram и ещё не удалены.
// emit_partial — отдать то, что уже есть, не дожидаясь остальных частей.
int pduCollect(const char *cmgl, SMS *out, int max_out, ConcatWait *waits,
               int max_waits, const int *skip_idx, int skip_n, uint32_t now_ms,
               uint32_t timeout_ms, bool emit_partial);
