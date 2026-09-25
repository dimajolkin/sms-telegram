#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

struct SMS {
  int index;
  char from[32];
  char text[400];
};

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
  int readUnreadSMS(SMS *out, int max_n);
  int listSMS(SMS *out, int max_n);
  bool deleteSMS(int index);
  bool sendSMS(const char *number, const char *text);
  bool ussd(const char *code, char *out, size_t out_len);

  bool signalQuality(int *csq, int *bars);
  void operatorName(char *out, size_t out_len);
  int smsCount();
  int missedCount();
  int listMissedCalls(MissedCall *out, int max_n);

  bool pollMissedCall(char *number, size_t number_len);

 private:
  HardwareSerial *uart_ = nullptr;
  SemaphoreHandle_t mu_ = nullptr;
  String line_buf_;
  bool ringing_ = false;
  char ring_num_[32] = {0};
  uint32_t last_ring_ms_ = 0;

  void lock();
  void unlock();
  void writeRaw(const char *s);
  void drain();
  void ingestUART();
  void noteURC(const char *line);
  bool readUntilOK(char *resp, size_t resp_len, uint32_t timeout_ms);
  bool syncAT(int tries);
  bool waitNetwork(uint32_t timeout_ms);
};

int parseCMGL(const char *resp, SMS *out, int max_n);
int parseCPBR(const char *resp, MissedCall *out, int max_n);
void decodeSMSText(const char *in, char *out, size_t out_len);
bool decodeUCS2Hex(const char *s, char *out, size_t out_len);
int csqToBars(int csq);
