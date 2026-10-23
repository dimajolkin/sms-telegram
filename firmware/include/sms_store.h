#pragma once

#include <stddef.h>
#include <stdint.h>

// Очередь SMS на разделе SPIFFS (1.5 МБ). 0 = не записалось.
bool smsStoreBegin();
uint32_t smsStoreSave(const char *from, const char *text);
bool smsStoreLoad(uint32_t id, char *from, size_t from_len, char *text,
                  size_t text_len);
void smsStoreRemove(uint32_t id);
void smsStoreUnclaim(uint32_t id);
int smsStoreClaim(uint32_t *ids, int max_n);
int smsStoreCount();
