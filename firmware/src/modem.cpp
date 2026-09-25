#include "modem.h"
#include "config.h"
#include "pins.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void onlyHexDigits(const char *s, char *out, size_t out_len) {
  size_t n = 0;
  for (const char *p = s; *p && n + 1 < out_len; p++) {
    char c = *p;
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
        (c >= 'a' && c <= 'f')) {
      out[n++] = c;
    } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      continue;
    } else {
      out[0] = 0;
      return;
    }
  }
  out[n] = 0;
  if (n < 4) out[0] = 0;
}

bool decodeUCS2Hex(const char *s, char *out, size_t out_len) {
  size_t len = strlen(s);
  if (len < 4 || (len % 4) != 0 || out_len < 2) return false;
  for (size_t i = 0; i < len; i++) {
    char c = s[i];
    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
          (c >= 'a' && c <= 'f')))
      return false;
  }
  size_t o = 0;
  for (size_t i = 0; i + 4 <= len && o + 4 < out_len; i += 4) {
    char hex[5] = {s[i], s[i + 1], s[i + 2], s[i + 3], 0};
    unsigned v = (unsigned)strtoul(hex, nullptr, 16);
    if (v < 0x80) {
      out[o++] = (char)v;
    } else if (v < 0x800) {
      out[o++] = (char)(0xC0 | (v >> 6));
      out[o++] = (char)(0x80 | (v & 0x3F));
    } else {
      out[o++] = (char)(0xE0 | (v >> 12));
      out[o++] = (char)(0x80 | ((v >> 6) & 0x3F));
      out[o++] = (char)(0x80 | (v & 0x3F));
    }
  }
  out[o] = 0;
  return o > 0;
}

void decodeSMSText(const char *in, char *out, size_t out_len) {
  if (!in || !out || out_len == 0) return;
  char compact[512];
  onlyHexDigits(in, compact, sizeof(compact));
  if (compact[0] && decodeUCS2Hex(compact, out, out_len)) return;
  strncpy(out, in, out_len - 1);
  out[out_len - 1] = 0;
}

int csqToBars(int csq) {
  if (csq == 99 || csq < 0) return 0;
  if (csq >= 20) return 4;
  if (csq >= 15) return 3;
  if (csq >= 10) return 2;
  if (csq >= 5) return 1;
  return 0;
}

int parseCMGL(const char *resp, SMS *out, int max_n) {
  if (!resp || !out || max_n <= 0) return 0;
  int n = 0;
  SMS cur;
  bool have = false;
  memset(&cur, 0, sizeof(cur));

  const char *p = resp;
  while (*p && n < max_n) {
    const char *eol = strpbrk(p, "\r\n");
    size_t len = eol ? (size_t)(eol - p) : strlen(p);
    char line[480];
    if (len >= sizeof(line)) len = sizeof(line) - 1;
    memcpy(line, p, len);
    line[len] = 0;
    while (len && (line[len - 1] == '\r' || line[len - 1] == ' '))
      line[--len] = 0;

    if (strncmp(line, "+CMGL:", 6) == 0) {
      if (have) {
        decodeSMSText(cur.text, cur.text, sizeof(cur.text));
        out[n++] = cur;
        if (n >= max_n) break;
      }
      memset(&cur, 0, sizeof(cur));
      have = true;
      // +CMGL: idx,"REC UNREAD","from",...
      char *c1 = strchr(line + 6, ',');
      if (c1) {
        cur.index = atoi(line + 6);
        char *c2 = strchr(c1 + 1, ',');
        if (c2) {
          char *c3 = strchr(c2 + 1, ',');
          char from[64] = {0};
          const char *fs = c2 + 1;
          while (*fs == ' ' || *fs == '"') fs++;
          size_t i = 0;
          while (*fs && *fs != '"' && i + 1 < sizeof(from)) from[i++] = *fs++;
          from[i] = 0;
          decodeSMSText(from, cur.from, sizeof(cur.from));
          (void)c3;
        }
      }
    } else if (have && line[0] && strcmp(line, "OK") != 0 &&
               strncmp(line, "AT", 2) != 0) {
      if (!cur.text[0]) {
        strncpy(cur.text, line, sizeof(cur.text) - 1);
      } else {
        size_t L = strlen(cur.text);
        strncat(cur.text, line, sizeof(cur.text) - L - 1);
      }
    }

    if (!eol) break;
    p = eol;
    while (*p == '\r' || *p == '\n') p++;
  }
  if (have && cur.text[0] && n < max_n) {
    decodeSMSText(cur.text, cur.text, sizeof(cur.text));
    out[n++] = cur;
  }
  return n;
}

int parseCPBR(const char *resp, MissedCall *out, int max_n) {
  if (!resp || !out || max_n <= 0) return 0;
  int n = 0;
  const char *p = resp;
  while (*p && n < max_n) {
    const char *eol = strpbrk(p, "\r\n");
    size_t len = eol ? (size_t)(eol - p) : strlen(p);
    char line[160];
    if (len >= sizeof(line)) len = sizeof(line) - 1;
    memcpy(line, p, len);
    line[len] = 0;

    if (strncmp(line, "+CPBR:", 6) == 0) {
      MissedCall m;
      memset(&m, 0, sizeof(m));
      char *rest = line + 6;
      while (*rest == ' ') rest++;
      m.index = atoi(rest);
      char *q1 = strchr(rest, '"');
      if (q1) {
        char *q2 = strchr(q1 + 1, '"');
        if (q2) {
          size_t nl = (size_t)(q2 - q1 - 1);
          if (nl >= sizeof(m.number)) nl = sizeof(m.number) - 1;
          memcpy(m.number, q1 + 1, nl);
          m.number[nl] = 0;
          char *q3 = strchr(q2 + 1, '"');
          if (q3) {
            char *q4 = strchr(q3 + 1, '"');
            if (q4) {
              size_t nl2 = (size_t)(q4 - q3 - 1);
              if (nl2 >= sizeof(m.name)) nl2 = sizeof(m.name) - 1;
              memcpy(m.name, q3 + 1, nl2);
              m.name[nl2] = 0;
            }
          }
          out[n++] = m;
        }
      }
    }
    if (!eol) break;
    p = eol;
    while (*p == '\r' || *p == '\n') p++;
  }
  return n;
}

void Modem::begin(HardwareSerial *uart, SemaphoreHandle_t mu) {
  uart_ = uart;
  mu_ = mu;
}

void Modem::lock() {
  if (mu_) xSemaphoreTake(mu_, portMAX_DELAY);
}

void Modem::unlock() {
  if (mu_) xSemaphoreGive(mu_);
}

void Modem::writeRaw(const char *s) {
  if (!uart_ || !s) return;
  uart_->print(s);
}

void Modem::resetHW() {
  pinMode(PIN_MODEM_RST, OUTPUT);
  digitalWrite(PIN_MODEM_RST, HIGH);
  Serial.println("modem RST idle HIGH");
}

void Modem::ingestUART() {
  while (uart_ && uart_->available()) {
    int b = uart_->read();
    if (b < 0) break;
    if (b == '\n' || b == '\r') {
      if (line_buf_.length()) {
        noteURC(line_buf_.c_str());
        line_buf_ = "";
      }
      continue;
    }
    if (line_buf_.length() < 160) line_buf_ += (char)b;
  }
}

void Modem::noteURC(const char *line) {
  while (*line == ' ') line++;
  if (!*line) return;
  if (strcmp(line, "RING") == 0 || strncmp(line, "+CRING:", 7) == 0) {
    ringing_ = true;
    last_ring_ms_ = millis();
    return;
  }
  if (strncmp(line, "+CLIP:", 6) == 0) {
    const char *rest = line + 6;
    while (*rest == ' ') rest++;
    const char *q1 = strchr(rest, '"');
    if (!q1) {
      ringing_ = true;
      last_ring_ms_ = millis();
      return;
    }
    const char *q2 = strchr(q1 + 1, '"');
    if (!q2) return;
    size_t n = (size_t)(q2 - q1 - 1);
    char raw[40] = {0};
    if (n >= sizeof(raw)) n = sizeof(raw) - 1;
    memcpy(raw, q1 + 1, n);
    decodeSMSText(raw, ring_num_, sizeof(ring_num_));
    ringing_ = true;
    last_ring_ms_ = millis();
  }
}

void Modem::drain() { ingestUART(); }

bool Modem::readUntilOK(char *resp, size_t resp_len, uint32_t timeout_ms) {
  if (!resp || resp_len == 0) return false;
  resp[0] = 0;
  size_t used = 0;
  uint32_t deadline = millis() + timeout_ms;
  while ((int32_t)(deadline - millis()) > 0) {
    while (uart_->available()) {
      int b = uart_->read();
      if (b < 0) break;
      if (used + 1 < resp_len) {
        resp[used++] = (char)b;
        resp[used] = 0;
      }
    }
    // URC inside response
    char *line = resp;
    for (;;) {
      char *eol = strpbrk(line, "\r\n");
      char tmp[180];
      if (eol) {
        size_t L = (size_t)(eol - line);
        if (L >= sizeof(tmp)) L = sizeof(tmp) - 1;
        memcpy(tmp, line, L);
        tmp[L] = 0;
        if (strcmp(tmp, "RING") == 0 || strncmp(tmp, "+CLIP:", 6) == 0 ||
            strncmp(tmp, "+CRING:", 7) == 0)
          noteURC(tmp);
        line = eol + 1;
      } else
        break;
    }
    if (strstr(resp, "\nOK") || strstr(resp, "\r\nOK") ||
        (used >= 2 && strcmp(resp + used - 2, "OK") == 0))
      return true;
    if (strstr(resp, "ERROR")) return false;
    delay(5);
  }
  return false;
}

bool Modem::syncAT(int tries) {
  if (tries < 1) tries = 20;
  if (tries > 20) tries = 20;
  for (int i = 0; i < tries; i++) {
    drain();
    writeRaw("AT\r");
    char resp[128];
    if (readUntilOK(resp, sizeof(resp), 1200)) return true;
    delay(100);
  }
  Serial.println("modem sync fail");
  return false;
}

bool Modem::waitNetwork(uint32_t timeout_ms) {
  Serial.println("modem wait network…");
  uint32_t deadline = millis() + timeout_ms;
  while ((int32_t)(deadline - millis()) > 0) {
    char resp[128];
    if (at("AT+CREG?", resp, sizeof(resp), 3000)) {
      if (strstr(resp, ",1") || strstr(resp, ",5")) {
        Serial.printf("modem network OK %s\n", resp);
        return true;
      }
    }
    delay(2000);
  }
  return false;
}

bool Modem::at(const char *cmd, char *resp, size_t resp_len,
               uint32_t timeout_ms) {
  lock();
  ingestUART();
  char full[96];
  snprintf(full, sizeof(full), "%s\r", cmd);
  writeRaw(full);
  bool ok = readUntilOK(resp, resp_len, timeout_ms);
  unlock();
  return ok;
}

bool Modem::selfTest() {
  Serial.println("modem selftest…");
  struct {
    const char *cmd;
    const char *need;
  } checks[] = {
      {"AT", nullptr},       {"ATI", nullptr},      {"AT+CSQ", "+CSQ:"},
      {"AT+CREG?", "+CREG:"}, {"AT+CPMS?", "+CPMS:"}, {"AT+CMGF?", "+CMGF:"},
  };
  for (auto &c : checks) {
    char resp[256];
    if (!at(c.cmd, resp, sizeof(resp), 4000)) {
      Serial.printf("modem selftest FAIL %s\n", c.cmd);
      return false;
    }
    if (c.need && !strstr(resp, c.need)) {
      Serial.printf("modem selftest FAIL %s missing %s\n", c.cmd, c.need);
      return false;
    }
    Serial.printf("modem selftest OK %s\n", c.cmd);
  }
  for (int i = 0; i < 3; i++) {
    char resp[64];
    if (!at("AT", resp, sizeof(resp), 2000)) return false;
  }
  Serial.println("modem selftest OK keepalive×3");
  return true;
}

bool Modem::init() {
  resetHW();
  if (!syncAT(12)) return false;
  Serial.println("modem synced");
  char resp[256];
  at("ATE0", resp, sizeof(resp), 2000);
  if (!at("AT+CMGF=1", resp, sizeof(resp), 3000)) return false;
  at("AT+CSCS=\"GSM\"", resp, sizeof(resp), 3000);
  at("AT+CNMI=2,1,0,0,0", resp, sizeof(resp), 3000);
  at("AT+CLIP=1", resp, sizeof(resp), 3000);
  at("AT+CRC=1", resp, sizeof(resp), 3000);
  if (!waitNetwork(90000)) return false;
  return selfTest();
}

int Modem::readUnreadSMS(SMS *out, int max_n) {
  char resp[2048];
  if (!at("AT+CMGL=\"REC UNREAD\"", resp, sizeof(resp), 10000)) return -1;
  return parseCMGL(resp, out, max_n);
}

int Modem::listSMS(SMS *out, int max_n) {
  char resp[3072];
  if (!at("AT+CMGL=\"ALL\"", resp, sizeof(resp), 15000)) return -1;
  return parseCMGL(resp, out, max_n);
}

bool Modem::deleteSMS(int index) {
  char cmd[32];
  snprintf(cmd, sizeof(cmd), "AT+CMGD=%d", index);
  char resp[64];
  return at(cmd, resp, sizeof(resp), 5000);
}

bool Modem::signalQuality(int *csq, int *bars) {
  char resp[128];
  if (!at("AT+CSQ", resp, sizeof(resp), 3000)) {
    if (csq) *csq = 99;
    if (bars) *bars = 0;
    return false;
  }
  int v = 99;
  const char *p = strstr(resp, "+CSQ:");
  if (p) v = atoi(p + 5);
  if (csq) *csq = v;
  if (bars) *bars = csqToBars(v);
  return true;
}

void Modem::operatorName(char *out, size_t out_len) {
  if (!out || out_len == 0) return;
  out[0] = 0;
  char resp[160];
  if (at("AT+COPS?", resp, sizeof(resp), 5000)) {
    const char *q1 = strchr(resp, '"');
    if (q1) {
      const char *q2 = strchr(q1 + 1, '"');
      if (q2 && q2 > q1 + 1) {
        size_t n = (size_t)(q2 - q1 - 1);
        if (n >= out_len) n = out_len - 1;
        memcpy(out, q1 + 1, n);
        out[n] = 0;
        return;
      }
    }
  }
  if (at("AT+CREG?", resp, sizeof(resp), 3000)) {
    if (strstr(resp, ",1") || strstr(resp, ",5")) {
      strncpy(out, "registered", out_len - 1);
      return;
    }
  }
  strncpy(out, "no net", out_len - 1);
}

int Modem::smsCount() {
  char resp[160];
  if (!at("AT+CPMS?", resp, sizeof(resp), 3000)) return -1;
  const char *p = strstr(resp, "+CPMS:");
  if (!p) return -1;
  // +CPMS: "SM",used,total,...
  const char *c = strchr(p, ',');
  if (!c) return -1;
  return atoi(c + 1);
}

int Modem::missedCount() {
  const char *stores[] = {"MC", "RC"};
  char resp[128];
  char discard[64];
  for (auto store : stores) {
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CPBS=\"%s\"", store);
    if (!at(cmd, resp, sizeof(resp), 3000)) continue;
    if (!at("AT+CPBS?", resp, sizeof(resp), 3000)) {
      at("AT+CPBS=\"SM\"", discard, sizeof(discard), 3000);
      continue;
    }
    at("AT+CPBS=\"SM\"", discard, sizeof(discard), 3000);
    const char *p = strstr(resp, "+CPBS:");
    if (!p) continue;
    const char *c = strchr(p, ',');
    if (!c) continue;
    return atoi(c + 1);
  }
  at("AT+CPBS=\"SM\"", resp, sizeof(resp), 3000);
  return -1;
}

int Modem::listMissedCalls(MissedCall *out, int max_n) {
  if (max_n <= 0) max_n = 20;
  const char *stores[] = {"MC", "RC"};
  char resp[1536];
  char discard[64];
  for (auto store : stores) {
    char cmd[40];
    snprintf(cmd, sizeof(cmd), "AT+CPBS=\"%s\"", store);
    if (!at(cmd, resp, sizeof(resp), 3000)) continue;
    snprintf(cmd, sizeof(cmd), "AT+CPBR=1,%d", max_n);
    bool ok = at(cmd, resp, sizeof(resp), 10000);
    at("AT+CPBS=\"SM\"", discard, sizeof(discard), 3000);
    if (!ok) continue;
    int n = parseCPBR(resp, out, max_n);
    if (n > 0 || strcmp(store, "MC") == 0) return n;
  }
  at("AT+CPBS=\"SM\"", discard, sizeof(discard), 3000);
  return 0;
}

bool Modem::pollMissedCall(char *number, size_t number_len) {
  lock();
  ingestUART();
  if (!ringing_) {
    unlock();
    return false;
  }
  if (millis() - last_ring_ms_ < MISSED_SILENCE_MS) {
    unlock();
    return false;
  }
  ringing_ = false;
  if (ring_num_[0]) {
    strncpy(number, ring_num_, number_len - 1);
    number[number_len - 1] = 0;
  } else {
    strncpy(number, "неизвестный", number_len - 1);
  }
  ring_num_[0] = 0;
  unlock();
  return true;
}

bool Modem::sendSMS(const char *number, const char *text) {
  lock();
  drain();
  char cmd[80];
  snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"\r", number);
  writeRaw(cmd);
  uint32_t deadline = millis() + 8000;
  bool got = false;
  while ((int32_t)(deadline - millis()) > 0) {
    while (uart_->available()) {
      int b = uart_->read();
      if (b == '>') {
        got = true;
        break;
      }
    }
    if (got) break;
    delay(20);
  }
  if (!got) {
    unlock();
    return false;
  }
  writeRaw(text);
  char ctrlz[2] = {0x1A, 0};
  writeRaw(ctrlz);
  char resp[256];
  bool ok = readUntilOK(resp, sizeof(resp), 60000);
  unlock();
  return ok && !strstr(resp, "ERROR");
}

bool Modem::ussd(const char *code, char *out, size_t out_len) {
  if (!out || out_len == 0) return false;
  out[0] = 0;
  lock();
  drain();
  char cmd[96];
  snprintf(cmd, sizeof(cmd), "AT+CUSD=1,\"%s\",15\r", code);
  writeRaw(cmd);
  uint32_t deadline = millis() + 45000;
  String buf;
  bool got_ok = false;
  while ((int32_t)(deadline - millis()) > 0) {
    while (uart_->available()) buf += (char)uart_->read();
    if (buf.indexOf("ERROR") >= 0 && buf.indexOf("+CUSD:") < 0) {
      unlock();
      return false;
    }
    int i = buf.indexOf("+CUSD:");
    if (i >= 0) {
      int eol = buf.indexOf('\n', i);
      if (eol < 0) eol = buf.indexOf('\r', i);
      if (eol >= 0) {
        String line = buf.substring(i, eol);
        int q1 = line.indexOf('"');
        if (q1 < 0) {
          strncpy(out, line.c_str(), out_len - 1);
          unlock();
          return true;
        }
        int q2 = line.indexOf('"', q1 + 1);
        if (q2 > q1) {
          String raw = line.substring(q1 + 1, q2);
          char compact[512];
          onlyHexDigits(raw.c_str(), compact, sizeof(compact));
          if (compact[0] && decodeUCS2Hex(compact, out, out_len)) {
            unlock();
            return true;
          }
          strncpy(out, raw.c_str(), out_len - 1);
          unlock();
          return true;
        }
      }
    }
    if (!got_ok && (buf.indexOf("\nOK") >= 0 || buf.indexOf("\r\nOK") >= 0))
      got_ok = true;
    delay(20);
  }
  unlock();
  return false;
}
