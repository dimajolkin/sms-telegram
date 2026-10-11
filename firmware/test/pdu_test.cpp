#include "pdu.h"

#include <stdio.h>
#include <string.h>

static int g_fail = 0;

static void expect(bool ok, const char *name) {
  if (ok) {
    printf("ok  %s\n", name);
  } else {
    printf("FAIL %s\n", name);
    g_fail++;
  }
}

static void setSeptet(uint8_t *ud, int septet_index, uint8_t val) {
  int bit = septet_index * 7;
  int bytei = bit / 8;
  int shift = bit % 8;
  uint32_t cur = ud[bytei] | ((uint32_t)ud[bytei + 1] << 8) |
                 ((uint32_t)ud[bytei + 2] << 16);
  cur &= ~(0x7Fu << shift);
  cur |= (uint32_t)(val & 0x7F) << shift;
  ud[bytei] = (uint8_t)(cur & 0xFF);
  ud[bytei + 1] = (uint8_t)((cur >> 8) & 0xFF);
  ud[bytei + 2] = (uint8_t)((cur >> 16) & 0xFF);
}

static int buildDeliver(uint8_t fo, uint8_t dcs, const uint8_t *ud, int udl,
                        bool gsm7, uint8_t *out) {
  int n = 0;
  out[n++] = 0x00;
  out[n++] = fo;
  out[n++] = 0x03;
  out[n++] = 0x81;
  out[n++] = 0x09;
  out[n++] = 0xF0;
  out[n++] = 0x00;
  out[n++] = dcs;
  for (int i = 0; i < 7; i++) out[n++] = 0;
  out[n++] = (uint8_t)udl;
  int octets = gsm7 ? (udl * 7 + 7) / 8 : udl;
  memcpy(out + n, ud, (size_t)octets);
  return n + octets;
}

static void toHex(const uint8_t *b, int n, char *out) {
  static const char *H = "0123456789ABCDEF";
  for (int i = 0; i < n; i++) {
    out[i * 2] = H[b[i] >> 4];
    out[i * 2 + 1] = H[b[i] & 0x0F];
  }
  out[n * 2] = 0;
}

static void addCmgl(char *dst, size_t cap, int index, const uint8_t *pdu, int n) {
  char hex[520];
  toHex(pdu, n, hex);
  char line[640];
  snprintf(line, sizeof(line), "+CMGL: %d,0,,%d\r\n%s\r\n", index, n, hex);
  if (strlen(dst) + strlen(line) + 1 < cap) strcat(dst, line);
}

static int utf8ToU16(const char *s, uint8_t *out, int maxb) {
  int o = 0;
  const unsigned char *p = (const unsigned char *)s;
  while (*p && o + 2 <= maxb) {
    uint32_t cp = 0;
    if (*p < 0x80) {
      cp = *p++;
    } else if ((*p & 0xE0) == 0xC0 && p[1]) {
      cp = (*p & 0x1F) << 6;
      p++;
      cp |= *p++ & 0x3F;
    } else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) {
      cp = (*p & 0x0F) << 12;
      p++;
      cp |= (*p & 0x3F) << 6;
      p++;
      cp |= *p++ & 0x3F;
    } else {
      p++;
      continue;
    }
    out[o++] = (uint8_t)((cp >> 8) & 0xFF);
    out[o++] = (uint8_t)(cp & 0xFF);
  }
  return o;
}

static void ucs2Segment(const uint8_t *u16, int nbytes, int ref, int total, int seq,
                        bool ref16, uint8_t *pdu, int *pdu_len) {
  uint8_t ud[180];
  int u = 0;
  if (total > 1) {
    if (!ref16) {
      ud[u++] = 5;
      ud[u++] = 0x00;
      ud[u++] = 3;
      ud[u++] = (uint8_t)ref;
      ud[u++] = (uint8_t)total;
      ud[u++] = (uint8_t)seq;
    } else {
      ud[u++] = 6;
      ud[u++] = 0x08;
      ud[u++] = 4;
      ud[u++] = (uint8_t)((ref >> 8) & 0xFF);
      ud[u++] = (uint8_t)(ref & 0xFF);
      ud[u++] = (uint8_t)total;
      ud[u++] = (uint8_t)seq;
    }
  }
  memcpy(ud + u, u16, (size_t)nbytes);
  u += nbytes;
  *pdu_len = buildDeliver(total > 1 ? 0x44 : 0x04, 0x08, ud, u, false, pdu);
}

int main() {
  SMS out[4];
  ConcatWait waits[CONCAT_WAIT_MAX];

  {
    const char *cmgl =
        "+CMGL: 3,0,,18\r\n"
        "0004038109F000000000000000000002C834\r\n"
        "OK\r\n";
    int n = pduCollect(cmgl, out, 4, nullptr, 0, nullptr, 0, 0, 20000, false);
    expect(n == 1 && strcmp(out[0].from, "900") == 0 && strcmp(out[0].text, "Hi") == 0 &&
               out[0].index == 3 && out[0].index_n == 1,
           "gsm7 single Hi");
  }

  {
    uint8_t u16[16];
    int nb = utf8ToU16("Да", u16, sizeof(u16));
    uint8_t pdu[64];
    int pn = 0;
    ucs2Segment(u16, nb, 0, 1, 1, false, pdu, &pn);
    char cmgl[256];
    cmgl[0] = 0;
    addCmgl(cmgl, sizeof(cmgl), 1, pdu, pn);
    int n = pduCollect(cmgl, out, 4, nullptr, 0, nullptr, 0, 0, 20000, false);
    expect(n == 1 && strcmp(out[0].text, "Да") == 0 && strcmp(out[0].from, "900") == 0,
           "ucs2 single");
  }

  {
    const char *text = "повышенной ставкой";
    uint8_t u16[64];
    int nb = utf8ToU16(text, u16, sizeof(u16));
    int chars = nb / 2;
    int cut = 12;
    uint8_t p1[128], p2[128];
    int n1 = 0, n2 = 0;
    ucs2Segment(u16, cut * 2, 0x2A, 2, 1, true, p1, &n1);
    ucs2Segment(u16 + cut * 2, (chars - cut) * 2, 0x2A, 2, 2, true, p2, &n2);
    char cmgl[800];
    cmgl[0] = 0;
    addCmgl(cmgl, sizeof(cmgl), 8, p2, n2);
    addCmgl(cmgl, sizeof(cmgl), 7, p1, n1);
    memset(waits, 0, sizeof(waits));
    int n = pduCollect(cmgl, out, 4, waits, CONCAT_WAIT_MAX, nullptr, 0, 1000, 20000,
                       false);
    expect(n == 1 && strcmp(out[0].text, text) == 0 && out[0].index_n == 2 &&
               out[0].indices[0] == 7 && out[0].indices[1] == 8,
           "ucs2 join mid-word, out of order");
  }

  {
    const char *text =
        "Дмитрий Владимирович, откройте «Накопительный счёт» с повышенной ставкой";
    uint8_t u16[400];
    int nb = utf8ToU16(text, u16, sizeof(u16));
    int chars = nb / 2;
    int cut = 66;
    if (chars <= cut) {
      expect(false, "fixture longer than 66");
    } else {
      uint8_t p1[256], p2[256];
      int n1 = 0, n2 = 0;
      ucs2Segment(u16, cut * 2, 7, 2, 1, true, p1, &n1);
      ucs2Segment(u16 + cut * 2, (chars - cut) * 2, 7, 2, 2, true, p2, &n2);
      char cmgl[1200];
      cmgl[0] = 0;
      addCmgl(cmgl, sizeof(cmgl), 1, p1, n1);
      addCmgl(cmgl, sizeof(cmgl), 2, p2, n2);
      int n = pduCollect(cmgl, out, 4, nullptr, 0, nullptr, 0, 0, 20000, false);
      expect(n == 1 && strcmp(out[0].text, text) == 0, "ucs2 66-char segment");
      if (n == 1 && strcmp(out[0].text, text) != 0)
        printf("  got: %s\n", out[0].text);
    }
  }

  {
    uint8_t u1[] = {0x00, 0x41, 0xD8, 0x3D};
    uint8_t u2[] = {0xDE, 0x00, 0x00, 0x42};
    uint8_t p1[64], p2[64];
    int n1 = 0, n2 = 0;
    ucs2Segment(u1, 4, 0x7F, 2, 1, false, p1, &n1);
    ucs2Segment(u2, 4, 0x7F, 2, 2, false, p2, &n2);
    char cmgl[500];
    cmgl[0] = 0;
    addCmgl(cmgl, sizeof(cmgl), 1, p1, n1);
    addCmgl(cmgl, sizeof(cmgl), 2, p2, n2);
    int n = pduCollect(cmgl, out, 4, nullptr, 0, nullptr, 0, 0, 20000, false);
    const unsigned char *t = (const unsigned char *)out[0].text;
    bool emoji = n == 1 && t[0] == 'A' && t[1] == 0xF0 && t[2] == 0x9F && t[3] == 0x98 &&
                 t[4] == 0x80 && t[5] == 'B';
    expect(emoji, "surrogate pair joined before decode");
  }

  {
    uint8_t ud[24];
    memset(ud, 0, sizeof(ud));
    ud[0] = 5;
    ud[1] = 0x00;
    ud[2] = 3;
    ud[3] = 0x11;
    ud[4] = 2;
    ud[5] = 1;
    int header_septets = (6 * 8 + 6) / 7;
    const uint8_t a[] = {0x48, 0x65, 0x6C};
    for (int i = 0; i < 3; i++) setSeptet(ud, header_septets + i, a[i]);
    int udl = header_septets + 3;
    uint8_t pdu[64];
    int pn = buildDeliver(0x44, 0x00, ud, udl, true, pdu);

    uint8_t udb[24];
    memset(udb, 0, sizeof(udb));
    udb[0] = 5;
    udb[1] = 0x00;
    udb[2] = 3;
    udb[3] = 0x11;
    udb[4] = 2;
    udb[5] = 2;
    const uint8_t b[] = {0x6C, 0x6F};
    for (int i = 0; i < 2; i++) setSeptet(udb, header_septets + i, b[i]);
    int udlb = header_septets + 2;
    uint8_t pdub[64];
    int pnb = buildDeliver(0x44, 0x00, udb, udlb, true, pdub);

    char cmgl[700];
    cmgl[0] = 0;
    addCmgl(cmgl, sizeof(cmgl), 4, pdub, pnb);
    addCmgl(cmgl, sizeof(cmgl), 3, pdu, pn);
    int n = pduCollect(cmgl, out, 4, nullptr, 0, nullptr, 0, 0, 20000, false);
    expect(n == 1 && strcmp(out[0].text, "Hello") == 0, "gsm7 concat across UDH fill");
    if (n == 1 && strcmp(out[0].text, "Hello") != 0) printf("  got: %s\n", out[0].text);
  }

  {
    uint8_t ud[24];
    memset(ud, 0, sizeof(ud));
    ud[0] = 5;
    ud[1] = 0;
    ud[2] = 3;
    ud[3] = 9;
    ud[4] = 2;
    ud[5] = 1;
    int hs = (6 * 8 + 6) / 7;
    setSeptet(ud, hs, 0x41);
    setSeptet(ud, hs + 1, 0x1B);
    uint8_t p1[64];
    int n1 = buildDeliver(0x44, 0x00, ud, hs + 2, true, p1);
    uint8_t udb[24];
    memset(udb, 0, sizeof(udb));
    udb[0] = 5;
    udb[1] = 0;
    udb[2] = 3;
    udb[3] = 9;
    udb[4] = 2;
    udb[5] = 2;
    setSeptet(udb, hs, 0x65);
    uint8_t p2[64];
    int n2 = buildDeliver(0x44, 0x00, udb, hs + 1, true, p2);
    char cmgl[700];
    cmgl[0] = 0;
    addCmgl(cmgl, sizeof(cmgl), 1, p1, n1);
    addCmgl(cmgl, sizeof(cmgl), 2, p2, n2);
    int n = pduCollect(cmgl, out, 4, nullptr, 0, nullptr, 0, 0, 20000, false);
    const unsigned char *t = (const unsigned char *)out[0].text;
    bool euro = n == 1 && t[0] == 'A' && t[1] == 0xE2 && t[2] == 0x82 && t[3] == 0xAC &&
                t[4] == 0;
    expect(euro, "gsm7 escape joined before decode");
  }

  {
    uint8_t u16[32];
    int nb = utf8ToU16("Привет", u16, sizeof(u16));
    uint8_t p1[80], p2[80];
    int n1 = 0, n2 = 0;
    ucs2Segment(u16, 6, 1, 2, 1, false, p1, &n1);
    ucs2Segment(u16 + 6, nb - 6, 1, 2, 2, false, p2, &n2);
    char only1[300];
    only1[0] = 0;
    addCmgl(only1, sizeof(only1), 5, p1, n1);
    char both[600];
    both[0] = 0;
    addCmgl(both, sizeof(both), 5, p1, n1);
    addCmgl(both, sizeof(both), 6, p2, n2);
    memset(waits, 0, sizeof(waits));
    int n = pduCollect(only1, out, 4, waits, CONCAT_WAIT_MAX, nullptr, 0, 0, 20000, false);
    expect(n == 0 && waits[0].used, "wait for missing part");
    n = pduCollect(both, out, 4, waits, CONCAT_WAIT_MAX, nullptr, 0, 1000, 20000, false);
    expect(n == 1 && strcmp(out[0].text, "Привет") == 0 && !waits[0].used,
           "second poll completes");
    int skip[2] = {out[0].indices[0], out[0].indices[1]};
    n = pduCollect(both, out, 4, waits, CONCAT_WAIT_MAX, skip, 2, 1500, 20000, false);
    expect(n == 0, "skip already queued parts");
  }

  {
    uint8_t u16[32];
    utf8ToU16("Привет", u16, sizeof(u16));
    uint8_t p1[80];
    int n1 = 0;
    ucs2Segment(u16, 6, 3, 2, 1, false, p1, &n1);
    char only1[300];
    only1[0] = 0;
    addCmgl(only1, sizeof(only1), 1, p1, n1);
    memset(waits, 0, sizeof(waits));
    int n = pduCollect(only1, out, 4, waits, CONCAT_WAIT_MAX, nullptr, 0, 0, 20000, false);
    expect(n == 0, "timeout not yet");
    n = pduCollect(only1, out, 4, waits, CONCAT_WAIT_MAX, nullptr, 0, 20000, 20000, false);
    expect(n == 1 && strcmp(out[0].text, "При") == 0, "timeout sends partial");
  }

  {
    uint8_t u16[32];
    utf8ToU16("Привет", u16, sizeof(u16));
    uint8_t p1[80];
    int n1 = 0;
    ucs2Segment(u16, 6, 3, 2, 1, false, p1, &n1);
    char only1[300];
    only1[0] = 0;
    addCmgl(only1, sizeof(only1), 1, p1, n1);
    int n = pduCollect(only1, out, 4, nullptr, 0, nullptr, 0, 0, 20000, true);
    expect(n == 1 && strcmp(out[0].text, "При") == 0, "inbox shows partial");
  }

  {
    const char *text = "9436 - никому не передавайте ваш код доступа к сервису МТС";
    uint8_t u16[220];
    int nb = utf8ToU16(text, u16, sizeof(u16));
    uint8_t ud[240];
    ud[0] = 0x06;
    ud[1] = 0x08;
    ud[2] = 0x04;
    ud[3] = 0x1D;
    ud[4] = 0x1D;
    ud[5] = 0x01;
    ud[6] = 0x01;
    memcpy(ud + 7, u16, (size_t)nb);
    int udl = 7 + nb;
    char hex[520];
    toHex(ud, udl, hex);
    char cmgl[800];
    snprintf(cmgl, sizeof(cmgl),
             "+CMGL: 4,\"REC UNREAD\",\"w44534119444\",,\"26/10/11,19:43:00+12\"\r\n"
             "%s\r\nOK\r\n",
             hex);
    int n = pduCollect(cmgl, out, 4, nullptr, 0, nullptr, 0, 0, 20000, false);
    expect(n == 1 && strcmp(out[0].from, "+44534119444") == 0 && strcmp(out[0].text, text) == 0,
           "text mode strips UDH and fixes sender");
    if (n == 1 && strcmp(out[0].text, text) != 0) printf("  got: %s\n", out[0].text);

    uint8_t pdu[300];
    int pn = 0;
    pdu[pn++] = 0x00;
    pdu[pn++] = 0x44;
    pdu[pn++] = 0x0B;
    pdu[pn++] = 0x91;
    pdu[pn++] = 0x44;
    pdu[pn++] = 0x35;
    pdu[pn++] = 0x14;
    pdu[pn++] = 0x91;
    pdu[pn++] = 0x44;
    pdu[pn++] = 0xF4;
    pdu[pn++] = 0x00;
    pdu[pn++] = 0x08;
    for (int i = 0; i < 7; i++) pdu[pn++] = 0;
    pdu[pn++] = (uint8_t)udl;
    memcpy(pdu + pn, ud, (size_t)udl);
    pn += udl;
    char pdu_cmgl[900];
    pdu_cmgl[0] = 0;
    addCmgl(pdu_cmgl, sizeof(pdu_cmgl), 9, pdu, pn);
    n = pduCollect(pdu_cmgl, out, 4, nullptr, 0, nullptr, 0, 0, 20000, false);
    expect(n == 1 && strcmp(out[0].from, "+44534119444") == 0 && strcmp(out[0].text, text) == 0,
           "pdu mode same MTS message");
    if (n == 1 && strcmp(out[0].text, text) != 0) printf("  got: %s\n", out[0].text);
  }

  {
    const char *text =
        "0986 - никому не передавайте ваш код доступа к сервису МТС. 12345678901";
    uint8_t u16[220];
    int nb = utf8ToU16(text, u16, sizeof(u16));
    uint8_t ud[240];
    ud[0] = 0x06;
    ud[1] = 0x08;
    ud[2] = 0x04;
    ud[3] = 0x22;
    ud[4] = 0x03;
    ud[5] = 0x01;
    ud[6] = 0x01;
    memcpy(ud + 7, u16, (size_t)nb);
    int udl = 7 + nb;
    uint8_t pdu[300];
    int pn = 0;
    pdu[pn++] = 0x00;
    pdu[pn++] = 0x64;
    pdu[pn++] = 0x0B;
    pdu[pn++] = 0xD0;
    pdu[pn++] = 0x4D;
    pdu[pn++] = 0xEA;
    pdu[pn++] = 0x30;
    pdu[pn++] = 0x92;
    pdu[pn++] = 0x24;
    pdu[pn++] = 0x02;
    pdu[pn++] = 0x00;
    pdu[pn++] = 0x08;
    for (int i = 0; i < 7; i++) pdu[pn++] = 0;
    pdu[pn++] = (uint8_t)udl;
    memcpy(pdu + pn, ud, (size_t)udl);
    pn += udl;
    expect(pn > 160, "alphanumeric PDU longer than 160 bytes");
    char pdu_cmgl[900];
    pdu_cmgl[0] = 0;
    addCmgl(pdu_cmgl, sizeof(pdu_cmgl), 2, pdu, pn);
    int n = pduCollect(pdu_cmgl, out, 4, nullptr, 0, nullptr, 0, 0, 20000, false);
    expect(n == 1 && strcmp(out[0].from, "MTC_ID") == 0 && strcmp(out[0].text, text) == 0,
           "pdu alphanumeric sender");
    if (n == 1 && strcmp(out[0].text, text) != 0) printf("  got: %s\n", out[0].text);

    char hex[700];
    toHex(pdu, pn, hex);
    char text_cmgl[900];
    snprintf(text_cmgl, sizeof(text_cmgl),
             "+CMGL: 2,\"REC UNREAD\",\"\",,\"26/10/11,20:23:00+12\"\r\n%s\r\nOK\r\n", hex);
    n = pduCollect(text_cmgl, out, 4, nullptr, 0, nullptr, 0, 0, 20000, false);
    expect(n == 1 && strcmp(out[0].from, "MTC_ID") == 0 && strcmp(out[0].text, text) == 0,
           "text mode decodes full PDU instead of hex dump");
    if (n == 1 && (strcmp(out[0].from, "MTC_ID") != 0 || strcmp(out[0].text, text) != 0))
      printf("  from='%s' text='%s'\n", out[0].from, out[0].text);
  }

  if (g_fail) {
    printf("%d failed\n", g_fail);
    return 1;
  }
  printf("all passed\n");
  return 0;
}
