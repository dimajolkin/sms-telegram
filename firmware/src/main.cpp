#include <Arduino.h>
#include <HardwareSerial.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

#include "buttons.h"
#include "commands.h"
#include "config.h"
#include "display.h"
#include "events.h"
#include "modem.h"
#include "pins.h"
#include "status.h"
#include "telegram.h"
#include "ui.h"
#include "wifi_ntp.h"

static HardwareSerial ModemUART(1);
static SemaphoreHandle_t g_modem_mu;
static QueueHandle_t g_to_tg;
static QueueHandle_t g_to_modem;
static SharedStatus g_status;
static Modem g_modem;
static Telegram g_tg;
static Display g_disp;
static Buttons g_btns;
static UI g_ui;

static char g_chat_id[32];
static SemaphoreHandle_t g_chat_mu;

static void chat_set(const char *id) {
  if (xSemaphoreTake(g_chat_mu, pdMS_TO_TICKS(200)) != pdTRUE) return;
  strncpy(g_chat_id, id ? id : "", sizeof(g_chat_id) - 1);
  g_chat_id[sizeof(g_chat_id) - 1] = 0;
  xSemaphoreGive(g_chat_mu);
}

static void chat_get(char *out, size_t out_len) {
  out[0] = 0;
  if (xSemaphoreTake(g_chat_mu, pdMS_TO_TICKS(100)) != pdTRUE) return;
  strncpy(out, g_chat_id, out_len - 1);
  out[out_len - 1] = 0;
  xSemaphoreGive(g_chat_mu);
}

static void enqueue_tg(const TgEvent &ev) {
  if (xQueueSend(g_to_tg, &ev, pdMS_TO_TICKS(500)) == pdTRUE)
    status_tg_queue_add(&g_status, 1);
}

static void enqueue_text(const char *text) {
  TgEvent ev{};
  ev.type = TG_EVT_TEXT;
  ev.sms_index = -1;
  strncpy(ev.text, text, sizeof(ev.text) - 1);
  enqueue_tg(ev);
}

static bool tg_send_and_ack(const char *dest, const char *text) {
  bool ok = g_tg.send(dest, text);
  if (ok) status_tg_queue_add(&g_status, -1);
  return ok;
}

// Первое число вида 123 / 123.45 / 123,45 из ответа USSD.
static bool parseBalanceAmount(const char *text, float *out) {
  if (!text || !out) return false;
  for (const char *p = text; *p; p++) {
    if (!isdigit((unsigned char)*p)) continue;
    char buf[32];
    size_t n = 0;
    bool seen_sep = false;
    while (*p && n + 1 < sizeof(buf)) {
      if (isdigit((unsigned char)*p)) {
        buf[n++] = *p++;
      } else if ((*p == '.' || *p == ',') && !seen_sep) {
        seen_sep = true;
        buf[n++] = '.';
        p++;
      } else {
        break;
      }
    }
    buf[n] = 0;
    if (n == 0) continue;
    char *end = nullptr;
    float v = strtof(buf, &end);
    if (end != buf) {
      *out = v;
      return true;
    }
  }
  return false;
}

static void maybeCheckBalance() {
  time_t now = time(nullptr);
  if (now < 1700000000) return;  // NTP ещё нет

  static Preferences prefs;
  static bool prefs_open = false;
  if (!prefs_open) {
    prefs.begin("sms-gw", false);
    prefs_open = true;
  }

  time_t last = (time_t)prefs.getULong("bal_chk", 0);
  if (last == 0) {
    if (millis() < BALANCE_FIRST_CHECK_AFTER_MS) return;
  } else if (now - last < BALANCE_CHECK_INTERVAL_SEC) {
    return;
  }

  Serial.println("balance: weekly check *100#…");
  char resp[400];
  if (!g_modem.ussd("*100#", resp, sizeof(resp)) || !resp[0]) {
    Serial.println("balance: ussd fail, retry in 1d");
    // не ждать ещё 7 дней при ошибке
    prefs.putULong("bal_chk",
                   (uint32_t)(now - BALANCE_CHECK_INTERVAL_SEC + 86400));
    return;
  }
  prefs.putULong("bal_chk", (uint32_t)now);

  float bal = 0;
  if (!parseBalanceAmount(resp, &bal)) {
    Serial.printf("balance: parse fail [%s]\n", resp);
    return;
  }
  Serial.printf("balance: %.2f\n", bal);
  if (bal < BALANCE_WARN_BELOW) {
    char msg[220];
    snprintf(msg, sizeof(msg),
             "Низкий баланс: %.2f ₽ (порог %.0f).\n%s", bal,
             BALANCE_WARN_BELOW, resp);
    enqueue_text(msg);
  }
}

static void handle_update(const TgUpdate &u) {
  if (!u.has_message || u.text.length() == 0) return;
  String text = u.text;
  text.trim();
  char from[32];
  snprintf(from, sizeof(from), "%lld", (long long)u.chat_id);

  if (text.startsWith("/start")) {
    chat_set(from);
    enqueue_text("Подписка активна.\n/help");
    return;
  }

  char bound[32];
  chat_get(bound, sizeof(bound));
  if (bound[0] && strcmp(bound, from) != 0) {
    TgEvent ev{};
    ev.type = TG_EVT_TEXT;
    ev.sms_index = -1;
    strncpy(ev.from, from, sizeof(ev.from) - 1);
    strncpy(ev.text, "Этот бот привязан к другому chat id.", sizeof(ev.text) - 1);
    // send to requester: stash chat in from, special — use send with from
    // simpler: send directly
    g_tg.send(from, "Этот бот привязан к другому chat id.");
    return;
  }
  if (!bound[0]) chat_set(from);

  if (cmdMatch(text, "help")) {
    enqueue_text(helpText().c_str());
  } else if (cmdMatch(text, "balance")) {
    ModemCmd cmd{};
    cmd.type = MODEM_CMD_USSD;
    strncpy(cmd.arg1, "*100#", sizeof(cmd.arg1) - 1);
    xQueueSend(g_to_modem, &cmd, pdMS_TO_TICKS(500));
    enqueue_text("USSD *100#…");
  } else if (cmdMatch(text, "missed")) {
    ModemCmd cmd{};
    cmd.type = MODEM_CMD_LIST_MISSED;
    xQueueSend(g_to_modem, &cmd, pdMS_TO_TICKS(500));
    enqueue_text("Читаю пропущенные…");
  } else if (text.startsWith("/sms ")) {
    String num, body;
    if (!parseSMSCmd(text, &num, &body)) {
      enqueue_text("Формат: /sms +79001112233 текст");
      return;
    }
    ModemCmd cmd{};
    cmd.type = MODEM_CMD_SEND_SMS;
    strncpy(cmd.arg1, num.c_str(), sizeof(cmd.arg1) - 1);
    strncpy(cmd.arg2, body.c_str(), sizeof(cmd.arg2) - 1);
    xQueueSend(g_to_modem, &cmd, pdMS_TO_TICKS(500));
    enqueue_text("Отправляю SMS…");
  } else if (text.startsWith("/ussd ")) {
    String code = text.substring(6);
    code.trim();
    if (code.length() == 0) {
      enqueue_text("Формат: /ussd *100#");
      return;
    }
    ModemCmd cmd{};
    cmd.type = MODEM_CMD_USSD;
    strncpy(cmd.arg1, code.c_str(), sizeof(cmd.arg1) - 1);
    xQueueSend(g_to_modem, &cmd, pdMS_TO_TICKS(500));
    char msg[80];
    snprintf(msg, sizeof(msg), "USSD %s…", code.c_str());
    enqueue_text(msg);
  } else {
    enqueue_text("Не знаю команду. /help");
  }
}

static void telegram_task(void *arg) {
  (void)arg;
  Serial.printf("telegram_task on core %d\n", xPortGetCoreID());
  int64_t offset = 0;
  int fails = 0;
  uint32_t last_poll = 0;

  // drain backlog once
  {
    TgUpdate ups[3];
    int n = 0;
    int64_t next = 0;
    if (g_tg.getUpdates(0, 0, ups, &n, 3, &next)) {
      offset = next;
      for (int i = 0; i < n; i++) handle_update(ups[i]);
    }
  }

  char chat[32];
  chat_get(chat, sizeof(chat));
  if (chat[0]) g_tg.send(chat, "ESP online. /help");

  for (;;) {
    TgEvent ev;
    while (xQueueReceive(g_to_tg, &ev, 0) == pdTRUE) {
      chat_get(chat, sizeof(chat));
      if (!chat[0] && ev.type != TG_EVT_TEXT) {
        // нет подписчика — вернуть в очередь, не терять
        xQueueSend(g_to_tg, &ev, 0);
        break;
      }
      const char *dest = chat;
      bool ok = false;
      if (ev.type == TG_EVT_SMS) {
        char msg[520];
        snprintf(msg, sizeof(msg), "SMS from %s\n\n%s", ev.from, ev.text);
        ok = tg_send_and_ack(dest, msg);
        if (ok && ev.sms_index >= 0) {
          g_modem.deleteSMS(ev.sms_index);  // шлюз: не храним на SIM
        } else if (!ok) {
          xQueueSend(g_to_tg, &ev, 0);
          break;
        }
      } else if (ev.type == TG_EVT_MISSED) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Пропущенный звонок\n\n%s", ev.from);
        ok = tg_send_and_ack(dest, msg);
        if (!ok) {
          xQueueSend(g_to_tg, &ev, 0);
          break;
        }
      } else {
        ok = tg_send_and_ack(dest, ev.text);
        if (!ok) {
          xQueueSend(g_to_tg, &ev, 0);
          break;
        }
      }
    }

    if (millis() - last_poll > TG_POLL_MS) {
      last_poll = millis();
      TgUpdate ups[2];
      int n = 0;
      int64_t next = offset;
      if (g_tg.getUpdates(offset, 1, ups, &n, 2, &next)) {
        fails = 0;
        status_set_wifi(&g_status, true);
        char ip[16];
        wifiLocalIP(ip, sizeof(ip));
        status_set_ip(&g_status, ip);
        offset = next;
        for (int i = 0; i < n; i++) handle_update(ups[i]);
      } else {
        fails++;
        status_set_wifi(&g_status, fails < 3);
        Serial.println("tg poll fail");
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

static void modem_task(void *arg) {
  (void)arg;
  Serial.printf("modem_task on core %d\n", xPortGetCoreID());
  uint32_t last_sms = 0;
  uint32_t last_bal_poll = 0;

  for (;;) {
    ModemCmd cmd;
    while (xQueueReceive(g_to_modem, &cmd, 0) == pdTRUE) {
      if (cmd.type == MODEM_CMD_SEND_SMS) {
        bool ok = g_modem.sendSMS(cmd.arg1, cmd.arg2);
        char msg[96];
        if (ok)
          snprintf(msg, sizeof(msg), "SMS отправлено на %s", cmd.arg1);
        else
          snprintf(msg, sizeof(msg), "Ошибка SMS");
        enqueue_text(msg);
      } else if (cmd.type == MODEM_CMD_USSD) {
        char resp[400];
        bool ok = g_modem.ussd(cmd.arg1, resp, sizeof(resp));
        if (!ok || !resp[0] || strcmp(resp, "OK") == 0)
          enqueue_text("USSD: пустой ответ или ошибка");
        else {
          char msg[480];
          snprintf(msg, sizeof(msg), "USSD\n%s", resp);
          enqueue_text(msg);
        }
      } else if (cmd.type == MODEM_CMD_LIST_MISSED) {
        MissedCall list[20];
        int n = g_modem.listMissedCalls(list, 20);
        int cnt = g_modem.missedCount();
        if (n < 0) {
          char msg[80];
          snprintf(msg, sizeof(msg), "Ошибка. Счётчик CALLS: %d", cnt);
          enqueue_text(msg);
        } else if (n == 0) {
          String msg = "Пропущенных в памяти модема нет.";
          if (cnt > 0) msg += "\nСчётчик CPBS=" + String(cnt);
          msg += "\nНовые звонки шлются сюда сами после гудков (CLIP).";
          enqueue_text(msg.c_str());
        } else {
          String b = "Пропущенные (" + String(n) + "):\n";
          for (int i = 0; i < n; i++) {
            b += String(list[i].index) + ". ";
            if (list[i].name[0]) {
              b += list[i].name;
              b += ' ';
            }
            b += list[i].number;
            b += '\n';
          }
          enqueue_text(b.c_str());
        }
      } else if (cmd.type == MODEM_CMD_REFRESH_STATUS) {
        int csq, bars;
        char op[24];
        g_modem.signalQuality(&csq, &bars);
        g_modem.operatorName(op, sizeof(op));
        status_update_modem(&g_status, bars, op, g_modem.smsCount(),
                            g_modem.missedCount());
      }
    }

    char num[32];
    if (g_modem.pollMissedCall(num, sizeof(num))) {
      Serial.printf("missed call: %s\n", num);
      TgEvent ev{};
      ev.type = TG_EVT_MISSED;
      ev.sms_index = -1;
      strncpy(ev.from, num, sizeof(ev.from) - 1);
      enqueue_tg(ev);
      g_ui.wake();
    }

    if (millis() - last_sms > SMS_POLL_MS) {
      last_sms = millis();
      SMS msgs[5];
      int n = g_modem.readUnreadSMS(msgs, 5);
      if (n > 0) {
        for (int i = 0; i < n; i++) {
          TgEvent ev{};
          ev.type = TG_EVT_SMS;
          ev.sms_index = msgs[i].index;
          strncpy(ev.from, msgs[i].from, sizeof(ev.from) - 1);
          strncpy(ev.text, msgs[i].text, sizeof(ev.text) - 1);
          enqueue_tg(ev);
          g_ui.wake();
        }
      }
    }

    if (millis() - last_bal_poll > BALANCE_POLL_MS) {
      last_bal_poll = millis();
      maybeCheckBalance();
    }

    vTaskDelay(pdMS_TO_TICKS(30));
  }
}

static void ui_task(void *arg) {
  (void)arg;
  Serial.printf("ui_task on core %d\n", xPortGetCoreID());
  for (;;) {
    g_ui.tick();
    if (g_btns.pending())
      vTaskDelay(pdMS_TO_TICKS(5));
    else
      vTaskDelay(pdMS_TO_TICKS(15));
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("sms-telegram boot (PlatformIO/Arduino)");
  Serial.println("stages: 1=display  2=WiFi  3=UART+modem  4=Telegram+tasks");

  if (!WIFI_SSID[0] || !TELEGRAM_BOT_TOKEN[0]) {
    Serial.println("fatal: set WIFI_SSID / TELEGRAM_BOT_TOKEN in secrets.ini");
    for (;;) delay(2000);
  }

  g_modem_mu = xSemaphoreCreateMutex();
  g_chat_mu = xSemaphoreCreateMutex();
  g_to_tg = xQueueCreate(16, sizeof(TgEvent));
  g_to_modem = xQueueCreate(4, sizeof(ModemCmd));
  status_init(&g_status);
  chat_set(TELEGRAM_CHAT_ID);

  pinMode(PIN_MODEM_RST, OUTPUT);
  digitalWrite(PIN_MODEM_RST, HIGH);

  Serial.println("--- stage1 SPI ---");
  g_disp.begin();
  g_disp.drawBoot("wifi…");
  Serial.println("--- stage1 OK display ---");

  Serial.println("--- stage2 WiFi ---");
  while (!wifiConnect()) {
    g_disp.drawBoot("wifi retry…");
    delay(3000);
  }
  status_set_wifi(&g_status, true);
  {
    char ip[16];
    wifiLocalIP(ip, sizeof(ip));
    status_set_ip(&g_status, ip);
  }
  g_disp.drawBoot("ntp…");
  ntpSync();
  g_disp.drawBoot("modem…");
  Serial.println("--- stage2 OK wifi ---");

  Serial.println("--- stage3 UART ---");
  ModemUART.setRxBufferSize(1024);
  ModemUART.begin(UART_BAUD, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);
  g_modem.begin(&ModemUART, g_modem_mu);
  for (;;) {
    Serial.println("modem init…");
    if (g_modem.init()) break;
    Serial.println("modem init fail, retry");
    delay(2000);
  }
  g_disp.drawBoot("telegram…");
  Serial.println("--- stage3 OK modem ---");

  Serial.println("--- stage4 Telegram ---");
  g_tg.begin(TELEGRAM_BOT_TOKEN);
  if (g_tg.ping())
    Serial.println("tg ok");
  else
    Serial.println("tg ping fail");
  if (g_tg.setMyCommands())
    Serial.println("tg menu ok");
  else
    Serial.println("tg menu fail");

  g_btns.begin();
  g_ui.begin(&g_disp, &g_btns, &g_modem, &g_status);
  g_ui.refreshStatus();
  g_ui.tick();

  xTaskCreatePinnedToCore(telegram_task, "tg", 12288, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(modem_task, "modem", 8192, nullptr, 1, nullptr, 1);
  xTaskCreatePinnedToCore(ui_task, "ui", 6144, nullptr, 2, nullptr, 1);

  Serial.println("--- stage4 OK ready ---");
}

void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); }
