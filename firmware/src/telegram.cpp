#include "telegram.h"
#include "commands.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <cctype>
#include <cstdio>
#include <cstring>

void Telegram::begin(const char *token) { token_ = token ? token : ""; }

String Telegram::apiURL(const char *method) const {
  return String("https://api.telegram.org/bot") + token_ + "/" + method;
}

bool Telegram::doRequest(const char *method, const char *url,
                         const char *payload, const char *content_type,
                         String *body_out, int *code_out, int max_body) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  https.setTimeout(45000);
  if (!https.begin(client, url)) {
    Serial.println("tg begin failed");
    return false;
  }
  https.setUserAgent("sms-telegram/2.0");
  int code;
  if (payload && content_type) {
    https.addHeader("Content-Type", content_type);
    code = https.POST((uint8_t *)payload, strlen(payload));
  } else if (strcmp(method, "POST") == 0) {
    code = https.POST("");
  } else {
    code = https.GET();
  }
  if (code_out) *code_out = code;
  String body = https.getString();
  if ((int)body.length() > max_body) body = body.substring(0, max_body);
  if (body_out) *body_out = body;
  https.end();
  return code > 0;
}

bool Telegram::send(const char *chat_id, const char *text) {
  if (!chat_id || !chat_id[0] || !text) return false;
  String url = apiURL("sendMessage");
  String form = "chat_id=" + String(chat_id) + "&text=";
  // URL-encode minimally
  for (const char *p = text; *p; p++) {
    char c = *p;
    if (c == ' ')
      form += '+';
    else if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' ||
             c == '~')
      form += c;
    else {
      char hex[8];
      snprintf(hex, sizeof(hex), "%%%02X", (unsigned char)c);
      form += hex;
    }
  }
  String body;
  int code = 0;
  if (!doRequest("POST", url.c_str(), form.c_str(),
                 "application/x-www-form-urlencoded", &body, &code, 512))
    return false;
  if (code >= 300) {
    Serial.printf("tg send http %d %s\n", code, body.c_str());
    return false;
  }
  return true;
}

bool Telegram::ping() {
  String url = apiURL("getMe");
  String body;
  int code = 0;
  if (!doRequest("GET", url.c_str(), nullptr, nullptr, &body, &code, 512))
    return false;
  if (code >= 300 || body.length() == 0 || body[0] != '{') {
    Serial.printf("tg getMe fail %d %s\n", code, body.c_str());
    return false;
  }
  Serial.printf("tg getMe: %s\n", body.substring(0, 80).c_str());
  return true;
}

bool Telegram::setMyCommands() {
  String payload = "{\"commands\":[";
  for (int i = 0; i < BOT_COMMANDS_N; i++) {
    if (i) payload += ',';
    payload += "{\"command\":\"";
    payload += BOT_COMMANDS[i].cmd;
    payload += "\",\"description\":\"";
    // escape quotes in desc
    for (const char *p = BOT_COMMANDS[i].desc; *p; p++) {
      if (*p == '"' || *p == '\\') payload += '\\';
      payload += *p;
    }
    payload += "\"}";
  }
  payload += "]}";
  String url = apiURL("setMyCommands");
  String body;
  int code = 0;
  if (!doRequest("POST", url.c_str(), payload.c_str(), "application/json",
                 &body, &code, 256))
    return false;
  return code < 300;
}

bool Telegram::getUpdates(int64_t offset, int timeout_sec, TgUpdate *out,
                          int *count, int max_n, int64_t *next_offset) {
  if (count) *count = 0;
  if (next_offset) *next_offset = offset;
  String url = apiURL("getUpdates");
  url += "?timeout=";
  url += timeout_sec;
  url += "&limit=";
  url += max_n > 0 ? max_n : 1;
  if (offset > 0) {
    url += "&offset=";
    url += String((long)offset);
  }
  String body;
  int code = 0;
  if (!doRequest("GET", url.c_str(), nullptr, nullptr, &body, &code, 4096))
    return false;
  if (code >= 300) return false;
  if (body.indexOf("\"result\":[]") >= 0) return true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("tg json: %s\n", err.c_str());
    return false;
  }
  if (!doc["ok"].as<bool>()) return false;
  JsonArray result = doc["result"].as<JsonArray>();
  int n = 0;
  int64_t next = offset;
  for (JsonObject u : result) {
    if (n >= max_n) break;
    out[n].update_id = u["update_id"] | 0;
    out[n].has_message = false;
    out[n].text = "";
    out[n].chat_id = 0;
    if (u["message"].is<JsonObject>()) {
      JsonObject m = u["message"];
      out[n].has_message = true;
      out[n].chat_id = m["chat"]["id"] | (int64_t)0;
      const char *t = m["text"] | "";
      out[n].text = t;
    }
    if (out[n].update_id >= next) next = out[n].update_id + 1;
    n++;
  }
  if (count) *count = n;
  if (next_offset) *next_offset = next;
  return true;
}
