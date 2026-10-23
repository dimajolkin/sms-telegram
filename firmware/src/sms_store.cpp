#include "sms_store.h"

#include <SPIFFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int STORE_MAX = 400;
static const size_t STORE_KEEP_FREE = 16384;

static SemaphoreHandle_t g_mu;
static uint32_t g_ids[STORE_MAX];
static uint8_t g_inflight[STORE_MAX];
static int g_n;
static uint32_t g_next = 1;
static bool g_ready;

static void lock() {
  if (g_mu) xSemaphoreTake(g_mu, portMAX_DELAY);
}

static void unlock() {
  if (g_mu) xSemaphoreGive(g_mu);
}

static void pathFor(uint32_t id, char *out, size_t n) {
  snprintf(out, n, "/m%lu", (unsigned long)id);
}

static int findId(uint32_t id) {
  for (int i = 0; i < g_n; i++)
    if (g_ids[i] == id) return i;
  return -1;
}

static void forgetAt(int i) {
  if (i < 0 || i >= g_n) return;
  g_ids[i] = g_ids[g_n - 1];
  g_inflight[i] = g_inflight[g_n - 1];
  g_n--;
}

bool smsStoreBegin() {
  g_mu = xSemaphoreCreateMutex();
  g_n = 0;
  g_next = 1;
  Serial.println("spiffs mount…");
  if (!SPIFFS.begin(false)) {
    Serial.println("spiffs format…");
    if (!SPIFFS.begin(true)) {
      Serial.println("spiffs fail");
      return false;
    }
  }
  File root = SPIFFS.open("/");
  if (root) {
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
      const char *name = f.name();
      if (!name) continue;
      if (name[0] == '/') name++;
      if (name[0] != 'm') continue;
      uint32_t id = (uint32_t)strtoul(name + 1, nullptr, 10);
      if (!id || g_n >= STORE_MAX) continue;
      g_ids[g_n] = id;
      g_inflight[g_n] = 0;
      g_n++;
      if (id >= g_next) g_next = id + 1;
    }
  }
  g_ready = true;
  Serial.printf("spiffs ok sms=%d free=%u\n", g_n,
                (unsigned)SPIFFS.totalBytes() - (unsigned)SPIFFS.usedBytes());
  return true;
}

uint32_t smsStoreSave(const char *from, const char *text) {
  if (!g_ready || !text) return 0;
  lock();
  if (g_n >= STORE_MAX) {
    unlock();
    Serial.println("sms flash full");
    return 0;
  }
  size_t free_b = SPIFFS.totalBytes() - SPIFFS.usedBytes();
  if (free_b < STORE_KEEP_FREE) {
    unlock();
    Serial.printf("sms flash nospace %u\n", (unsigned)free_b);
    return 0;
  }
  uint32_t id = g_next++;
  char path[16];
  pathFor(id, path, sizeof(path));
  File f = SPIFFS.open(path, FILE_WRITE);
  if (!f) {
    unlock();
    Serial.println("sms flash open fail");
    return 0;
  }
  uint8_t from_len = 0;
  if (from) {
    size_t n = strlen(from);
    if (n > 31) n = 31;
    from_len = (uint8_t)n;
  }
  uint16_t text_len = (uint16_t)strlen(text);
  bool ok = f.write(&from_len, 1) == 1;
  if (ok && from_len) ok = f.write((const uint8_t *)from, from_len) == from_len;
  if (ok) ok = f.write((const uint8_t *)&text_len, 2) == 2;
  if (ok && text_len) ok = f.write((const uint8_t *)text, text_len) == text_len;
  f.close();
  if (!ok) {
    SPIFFS.remove(path);
    unlock();
    Serial.println("sms flash write fail");
    return 0;
  }
  g_ids[g_n] = id;
  g_inflight[g_n] = 1;
  g_n++;
  unlock();
  Serial.printf("sms flash save %u (%u bytes)\n", id, (unsigned)text_len);
  return id;
}

bool smsStoreLoad(uint32_t id, char *from, size_t from_len, char *text,
                  size_t text_len) {
  if (!g_ready || !id || !from || !text || from_len == 0 || text_len == 0)
    return false;
  from[0] = 0;
  text[0] = 0;
  char path[16];
  pathFor(id, path, sizeof(path));
  lock();
  File f = SPIFFS.open(path, FILE_READ);
  if (!f) {
    unlock();
    return false;
  }
  uint8_t fl = 0;
  uint16_t tl = 0;
  char from_buf[32];
  from_buf[0] = 0;
  bool ok = f.read(&fl, 1) == 1;
  if (ok && fl > 31) ok = false;
  if (ok && fl && f.read((uint8_t *)from_buf, fl) != fl) ok = false;
  if (ok) from_buf[fl] = 0;
  if (ok && f.read((uint8_t *)&tl, 2) != 2) ok = false;
  if (ok && tl >= text_len) tl = (uint16_t)(text_len - 1);
  if (ok && tl && f.read((uint8_t *)text, tl) != tl) ok = false;
  f.close();
  unlock();
  if (!ok) return false;
  text[tl] = 0;
  strncpy(from, from_buf, from_len - 1);
  from[from_len - 1] = 0;
  return true;
}

void smsStoreRemove(uint32_t id) {
  if (!g_ready || !id) return;
  char path[16];
  pathFor(id, path, sizeof(path));
  lock();
  SPIFFS.remove(path);
  int i = findId(id);
  if (i >= 0) forgetAt(i);
  unlock();
  Serial.printf("sms flash drop %u\n", id);
}

void smsStoreUnclaim(uint32_t id) {
  if (!g_ready || !id) return;
  lock();
  int i = findId(id);
  if (i >= 0) g_inflight[i] = 0;
  unlock();
}

int smsStoreClaim(uint32_t *ids, int max_n) {
  if (!g_ready || !ids || max_n <= 0) return 0;
  lock();
  int n = 0;
  for (int i = 0; i < g_n && n < max_n; i++) {
    if (g_inflight[i]) continue;
    g_inflight[i] = 1;
    ids[n++] = g_ids[i];
  }
  unlock();
  return n;
}

int smsStoreCount() {
  lock();
  int n = g_n;
  unlock();
  return n;
}
