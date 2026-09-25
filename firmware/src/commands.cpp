#include "commands.h"

const BotCommand BOT_COMMANDS[] = {
    {"balance", "Баланс (*100#)", "/balance — баланс (*100#)"},
    {"missed", "Пропущенные звонки", "/missed — список пропущенных звонков"},
    {"sms", "SMS: /sms +79… текст", "/sms +79001112233 текст — отправить SMS"},
    {"ussd", "USSD: /ussd *код#", "/ussd *100# — произвольный USSD"},
    {"help", "Справка", "/help — справка"},
};

const int BOT_COMMANDS_N = sizeof(BOT_COMMANDS) / sizeof(BOT_COMMANDS[0]);

String helpText() {
  String b = "Команды:\n";
  for (int i = 0; i < BOT_COMMANDS_N; i++) {
    b += BOT_COMMANDS[i].help;
    b += '\n';
  }
  b += "\nВходящие SMS и пропущенные звонки приходят сюда автоматически.\n";
  b += "На экране: Next=inbox, OK=открыть/назад.";
  return b;
}

bool cmdMatch(const String &text, const char *name) {
  String a = String("/") + name;
  if (text == a) return true;
  String b = a + "@";
  return text.startsWith(b);
}

bool parseSMSCmd(const String &text, String *number, String *body) {
  if (!text.startsWith("/sms ")) return false;
  String rest = text.substring(5);
  rest.trim();
  int sp = rest.indexOf(' ');
  if (sp < 0) return false;
  *number = rest.substring(0, sp);
  *body = rest.substring(sp + 1);
  number->trim();
  body->trim();
  return number->length() > 0 && body->length() > 0;
}
