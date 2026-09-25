#pragma once

#include <Arduino.h>

struct TgUpdate {
  int64_t update_id;
  int64_t chat_id;
  String text;
  bool has_message;
};

class Telegram {
 public:
  void begin(const char *token);
  bool ping();
  bool setMyCommands();
  bool send(const char *chat_id, const char *text);
  bool getUpdates(int64_t offset, int timeout_sec, TgUpdate *out, int *count,
                  int max_n, int64_t *next_offset);

 private:
  String token_;
  String apiURL(const char *method) const;
  bool doRequest(const char *method, const char *url, const char *payload,
                 const char *content_type, String *body_out, int *code_out,
                 int max_body);
};
