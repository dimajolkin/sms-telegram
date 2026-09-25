#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

struct SharedStatus {
  bool wifi_ok;
  int bars;
  char oper[24];
  int sms_count;      // остаток на SIM (для отладки)
  int missed_count;
  int tg_queue;       // ждёт / в полёте в Telegram
  char ip[16];
  SemaphoreHandle_t mu;
};

void status_init(SharedStatus *s);
void status_set_wifi(SharedStatus *s, bool ok);
void status_set_ip(SharedStatus *s, const char *ip);
void status_tg_queue_add(SharedStatus *s, int delta);
int status_tg_queue_get(SharedStatus *s);
void status_snapshot(SharedStatus *s, bool *wifi_ok, int *bars, char *oper,
                     size_t oper_len, int *sms, int *missed, int *tg_queue,
                     char *ip, size_t ip_len);
void status_update_modem(SharedStatus *s, int bars, const char *oper, int sms,
                         int missed);
