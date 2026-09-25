#include "status.h"
#include <string.h>

void status_init(SharedStatus *s) {
  memset(s, 0, sizeof(*s));
  s->sms_count = -1;
  s->missed_count = -1;
  s->tg_queue = 0;
  s->mu = xSemaphoreCreateMutex();
}

void status_set_wifi(SharedStatus *s, bool ok) {
  if (xSemaphoreTake(s->mu, pdMS_TO_TICKS(100)) != pdTRUE) return;
  s->wifi_ok = ok;
  xSemaphoreGive(s->mu);
}

void status_set_ip(SharedStatus *s, const char *ip) {
  if (xSemaphoreTake(s->mu, pdMS_TO_TICKS(100)) != pdTRUE) return;
  if (ip) {
    strncpy(s->ip, ip, sizeof(s->ip) - 1);
    s->ip[sizeof(s->ip) - 1] = 0;
  } else {
    s->ip[0] = 0;
  }
  xSemaphoreGive(s->mu);
}

void status_tg_queue_add(SharedStatus *s, int delta) {
  if (xSemaphoreTake(s->mu, pdMS_TO_TICKS(100)) != pdTRUE) return;
  s->tg_queue += delta;
  if (s->tg_queue < 0) s->tg_queue = 0;
  xSemaphoreGive(s->mu);
}

int status_tg_queue_get(SharedStatus *s) {
  int n = 0;
  if (xSemaphoreTake(s->mu, pdMS_TO_TICKS(50)) != pdTRUE) return 0;
  n = s->tg_queue;
  xSemaphoreGive(s->mu);
  return n;
}

void status_snapshot(SharedStatus *s, bool *wifi_ok, int *bars, char *oper,
                     size_t oper_len, int *sms, int *missed, int *tg_queue,
                     char *ip, size_t ip_len) {
  if (xSemaphoreTake(s->mu, pdMS_TO_TICKS(100)) != pdTRUE) return;
  if (wifi_ok) *wifi_ok = s->wifi_ok;
  if (bars) *bars = s->bars;
  if (oper && oper_len) {
    strncpy(oper, s->oper, oper_len - 1);
    oper[oper_len - 1] = 0;
  }
  if (sms) *sms = s->sms_count;
  if (missed) *missed = s->missed_count;
  if (tg_queue) *tg_queue = s->tg_queue;
  if (ip && ip_len) {
    strncpy(ip, s->ip, ip_len - 1);
    ip[ip_len - 1] = 0;
  }
  xSemaphoreGive(s->mu);
}

void status_update_modem(SharedStatus *s, int bars, const char *oper, int sms,
                         int missed) {
  if (xSemaphoreTake(s->mu, pdMS_TO_TICKS(200)) != pdTRUE) return;
  s->bars = bars;
  if (oper) {
    strncpy(s->oper, oper, sizeof(s->oper) - 1);
    s->oper[sizeof(s->oper) - 1] = 0;
  }
  s->sms_count = sms;
  s->missed_count = missed;
  xSemaphoreGive(s->mu);
}
