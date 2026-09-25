#include "wifi_ntp.h"
#include "config.h"
#include <WiFi.h>
#include <string.h>
#include <time.h>

bool wifiConnect() {
  if (!WIFI_SSID[0]) {
    Serial.println("wifi: WIFI_SSID empty");
    return false;
  }
  Serial.printf("wifi connecting: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  for (int i = 0; i < 60; i++) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("wifi ok ip=%s\n", WiFi.localIP().toString().c_str());
      return true;
    }
    delay(500);
    Serial.print('.');
  }
  Serial.println("\nwifi timeout");
  return false;
}

void wifiLocalIP(char *out, size_t out_len) {
  if (!out || out_len == 0) return;
  out[0] = 0;
  if (WiFi.status() != WL_CONNECTED) return;
  String ip = WiFi.localIP().toString();
  strncpy(out, ip.c_str(), out_len - 1);
  out[out_len - 1] = 0;
}

bool ntpSync() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  for (int i = 0; i < 30; i++) {
    time_t now = time(nullptr);
    if (now > 1700000000) {
      struct tm tm;
      gmtime_r(&now, &tm);
      Serial.printf("ntp ok: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                    tm.tm_min, tm.tm_sec);
      return true;
    }
    delay(500);
  }
  Serial.println("ntp failed");
  return false;
}
