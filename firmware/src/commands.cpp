#include "commands.h"

const BotCommand BOT_COMMANDS[] = {
    {"balance", "Баланс (*100#)", "/balance — баланс (*100#)"},
    {"missed", "Пропущенные звонки", "/missed — список пропущенных звонков"},
    {"sms", "SMS: /sms +79… текст", "/sms +79001112233 текст — отправить SMS"},
    {"getsms", "Забрать SMS сейчас", "/get-sms — сразу забрать входящие SMS"},
    {"ussd", "USSD: /ussd *код#", "/ussd *100# — произвольный USSD"},
    {"call", "Звонок сброс",
     "/call — звонок сброс: спрошу номер и дождусь, пока сбросят (~5 с)"},
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

static bool phoneSep(const char *p, int *n) {
  unsigned char c = (unsigned char)p[0];
  if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '-' || c == '(' ||
      c == ')' || c == '.') {
    *n = 1;
    return true;
  }
  unsigned char c1 = (unsigned char)p[1];
  unsigned char c2 = (unsigned char)p[2];
  if (c == 0xC2 && (c1 == 0xA0 || c1 == 0xAD)) {
    *n = 2;
    return true;
  }
  if (c == 0xE2 && c1 == 0x80 &&
      (c2 == 0x87 || c2 == 0x89 || c2 == 0x8A || c2 == 0xAF ||
       (c2 >= 0x90 && c2 <= 0x95))) {
    *n = 3;
    return true;
  }
  if (c == 0xE2 && c1 == 0x88 && c2 == 0x92) {
    *n = 3;
    return true;
  }
  return false;
}

bool parsePhone(const String &text, char *out, size_t out_len) {
  if (!out || out_len < 4) return false;
  const char *in = text.c_str();
  int sep = 0;
  while (*in && phoneSep(in, &sep)) in += sep;
  bool plus = false;
  if (*in == '+') {
    plus = true;
    in++;
  }
  size_t digits = 0;
  for (const char *p = in; *p;) {
    if (*p >= '0' && *p <= '9') {
      digits++;
      if (digits > 15) return false;
      p++;
      continue;
    }
    if (phoneSep(p, &sep)) {
      p += sep;
      continue;
    }
    return false;
  }
  if (digits < 10) return false;
  size_t o = 0;
  if (plus) {
    if (o + 1 >= out_len) return false;
    out[o++] = '+';
  }
  for (const char *p = in; *p && o + 1 < out_len;) {
    if (*p >= '0' && *p <= '9') {
      out[o++] = *p++;
      continue;
    }
    if (!phoneSep(p, &sep)) break;
    p += sep;
  }
  out[o] = 0;
  return digits >= 10 && o == digits + (plus ? 1 : 0);
}
