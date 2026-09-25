#pragma once

#include <Arduino.h>

struct BotCommand {
  const char *cmd;
  const char *desc;
  const char *help;
};

extern const BotCommand BOT_COMMANDS[];
extern const int BOT_COMMANDS_N;

String helpText();
bool cmdMatch(const String &text, const char *name);
bool parseSMSCmd(const String &text, String *number, String *body);
