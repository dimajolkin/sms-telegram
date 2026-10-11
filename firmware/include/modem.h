#pragma once

#include "pdu.h"

#include <Arduino.h>
#include <HardwareSerial.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

struct MissedCall {
  int index;
  char number[32];
  char name[32];
};

class Modem {
 public:
  void begin(HardwareSerial *uart, SemaphoreHandle_t mu);
  bool init();
  bool selfTest();
  void resetHW();

  bool at(const char *cmd, char *resp, size_t resp_len, uint32_t timeout_ms);
  int readUnreadSMS(SMS *out, int max_n, bool immediate = false);
  int listSMS(SMS *out, int max_n);
  bool deleteSMS(int index);
  void releaseSMS(const SMS &s);
  bool sendSMS(const char *number, const char *text);
  bool ussd(const char *code, char *out, size_t out_len);

  bool signalQuality(int *csq, int *bars);
  void operatorName(char *out, size_t out_len);
  int smsCount();
  int missedCount();
  int listMissedCalls(MissedCall *out, int max_n);

  bool pollMissedCall(char *number, size_t number_len);
  bool smsWaiting();

 private:
  HardwareSerial *uart_ = nullptr;
  SemaphoreHandle_t mu_ = nullptr;
  String line_buf_;
  bool ringing_ = false;
  bool sms_waiting_ = false;
  char ring_num_[32] = {0};
  uint32_t last_ring_ms_ = 0;
  static const int QUEUED_MAX = 24;
  int queued_[QUEUED_MAX] = {};
  int queued_n_ = 0;
  ConcatWait waits_[CONCAT_WAIT_MAX] = {};

  void lock();
  void unlock();
  void writeRaw(const char *s);
  void drain();
  void ingestUART();
  void noteURC(const char *line);
  bool readUntilOK(char *resp, size_t resp_len, uint32_t timeout_ms);
  bool atHold(const char *cmd, char *resp, size_t resp_len, uint32_t timeout_ms);
  bool syncAT(int tries);
  bool waitNetwork(uint32_t timeout_ms);
  void forgetQueuedUnlocked(int index);
  void rememberQueuedUnlocked(const SMS *msgs, int n);
  int readPduList(const char *stat, bool forward, SMS *out, int max_n,
                  bool immediate);
};

int parseCMGL(const char *resp, SMS *out, int max_n);
int parseCPBR(const char *resp, MissedCall *out, int max_n);
void decodeSMSText(const char *in, char *out, size_t out_len);
bool decodeUCS2Hex(const char *s, char *out, size_t out_len);
int csqToBars(int csq);
