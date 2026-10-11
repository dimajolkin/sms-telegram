#include "pdu.h"

#include <stdlib.h>
#include <string.h>

static const uint16_t kGsm7[128] = {
    0x0040, 0x00A3, 0x0024, 0x00A5, 0x00E8, 0x00E9, 0x00F9, 0x00EC, 0x00F2,
    0x00C7, 0x000A, 0x00D8, 0x00F8, 0x000D, 0x00C5, 0x00E5, 0x0394, 0x005F,
    0x03A6, 0x0393, 0x039B, 0x03A9, 0x03A0, 0x03A8, 0x03A3, 0x0398, 0x039E,
    0x001B, 0x00C6, 0x00E6, 0x00DF, 0x00C9, 0x0020, 0x0021, 0x0022, 0x0023,
    0x00A4, 0x0025, 0x0026, 0x0027, 0x0028, 0x0029, 0x002A, 0x002B, 0x002C,
    0x002D, 0x002E, 0x002F, 0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035,
    0x0036, 0x0037, 0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E,
    0x003F, 0x00A1, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
    0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F, 0x0050,
    0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057, 0x0058, 0x0059,
    0x005A, 0x00C4, 0x00D6, 0x00D1, 0x00DC, 0x00A7, 0x00BF, 0x0061, 0x0062,
    0x0063, 0x0064, 0x0065, 0x0066, 0x0067, 0x0068, 0x0069, 0x006A, 0x006B,
    0x006C, 0x006D, 0x006E, 0x006F, 0x0070, 0x0071, 0x0072, 0x0073, 0x0074,
    0x0075, 0x0076, 0x0077, 0x0078, 0x0079, 0x007A, 0x00E4, 0x00F6, 0x00F1,
    0x00FC, 0x00E0,
};

struct PduPart {
  int index;
  char from[32];
  int ref;
  int total;
  int seq;
  int alphabet;
  int data_len;
  uint8_t data[160];
};

struct PartGroup {
  int idx[SMS_PARTS_MAX];
  int n;
  int ref;
  int total;
  int alphabet;
  char from[32];
  bool concat;
};

static void appendCp(char *out, size_t cap, size_t *o, uint32_t cp) {
  unsigned char tmp[4];
  int n = 0;
  if (cp < 0x80) {
    tmp[0] = (unsigned char)cp;
    n = 1;
  } else if (cp < 0x800) {
    tmp[0] = (unsigned char)(0xC0 | (cp >> 6));
    tmp[1] = (unsigned char)(0x80 | (cp & 0x3F));
    n = 2;
  } else if (cp < 0x10000) {
    tmp[0] = (unsigned char)(0xE0 | (cp >> 12));
    tmp[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    tmp[2] = (unsigned char)(0x80 | (cp & 0x3F));
    n = 3;
  } else if (cp < 0x110000) {
    tmp[0] = (unsigned char)(0xF0 | (cp >> 18));
    tmp[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
    tmp[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    tmp[3] = (unsigned char)(0x80 | (cp & 0x3F));
    n = 4;
  } else {
    return;
  }
  if (*o + (size_t)n + 1 > cap) return;
  memcpy(out + *o, tmp, (size_t)n);
  *o += (size_t)n;
  out[*o] = 0;
}

static uint16_t gsmExt(uint8_t c) {
  switch (c) {
    case 0x0A:
      return 0x000C;
    case 0x14:
      return 0x005E;
    case 0x28:
      return 0x007B;
    case 0x29:
      return 0x007D;
    case 0x2F:
      return 0x005C;
    case 0x3C:
      return 0x005B;
    case 0x3D:
      return 0x007E;
    case 0x3E:
      return 0x005D;
    case 0x40:
      return 0x007C;
    case 0x65:
      return 0x20AC;
    default:
      return 0;
  }
}

static void decodeGsm7(const uint8_t *s, int n, char *out, size_t cap) {
  size_t o = 0;
  if (cap == 0) return;
  out[0] = 0;
  for (int i = 0; i < n; i++) {
    uint8_t c = s[i] & 0x7F;
    if (c == 0x1B) {
      if (i + 1 >= n) break;
      uint16_t u = gsmExt(s[++i] & 0x7F);
      if (u) appendCp(out, cap, &o, u);
      continue;
    }
    appendCp(out, cap, &o, kGsm7[c]);
  }
}

static void decodeUtf16Be(const uint8_t *d, int len, char *out, size_t cap) {
  size_t o = 0;
  if (cap == 0) return;
  out[0] = 0;
  for (int i = 0; i + 1 < len; i += 2) {
    uint32_t u = ((uint32_t)d[i] << 8) | d[i + 1];
    if (u >= 0xD800 && u <= 0xDBFF && i + 3 < len) {
      uint32_t v = ((uint32_t)d[i + 2] << 8) | d[i + 3];
      if (v >= 0xDC00 && v <= 0xDFFF) {
        u = 0x10000 + ((u - 0xD800) << 10) + (v - 0xDC00);
        i += 2;
      } else {
        continue;
      }
    } else if (u >= 0xD800 && u <= 0xDFFF) {
      continue;
    }
    appendCp(out, cap, &o, u);
  }
}

static void decode8bit(const uint8_t *d, int len, char *out, size_t cap) {
  size_t o = 0;
  if (cap == 0) return;
  out[0] = 0;
  for (int i = 0; i < len; i++) appendCp(out, cap, &o, d[i]);
}

static int alphabetOf(uint8_t dcs) {
  if ((dcs & 0xC0) == 0x00) return (dcs >> 2) & 3;
  if ((dcs & 0xF0) == 0xF0) return (dcs & 0x04) ? 1 : 0;
  if ((dcs & 0xF0) == 0xE0) return 2;
  if ((dcs & 0xC0) == 0xC0) return 0;
  return 0;
}

static uint8_t getSeptet(const uint8_t *ud, int ud_octets, int septet_index) {
  int bit = septet_index * 7;
  int bytei = bit / 8;
  int shift = bit % 8;
  if (bytei >= ud_octets) return 0;
  uint32_t cur = ud[bytei];
  if (bytei + 1 < ud_octets) cur |= (uint32_t)ud[bytei + 1] << 8;
  return (uint8_t)((cur >> shift) & 0x7F);
}

static int takeAddr(const uint8_t *b, int len, int *i, char *from,
                    size_t from_len) {
  if (*i + 2 > len || from_len == 0) return -1;
  int digits = b[(*i)++];
  int typ = b[(*i)++];
  int nbytes = (digits + 1) / 2;
  if (nbytes < 0 || *i + nbytes > len) return -1;
  const uint8_t *a = b + *i;
  *i += nbytes;
  from[0] = 0;
  if ((typ & 0x70) == 0x50) {
    int septets = (digits * 4) / 7;
    uint8_t tmp[24];
    if (septets > (int)sizeof(tmp)) septets = (int)sizeof(tmp);
    for (int s = 0; s < septets; s++) tmp[s] = getSeptet(a, nbytes, s);
    decodeGsm7(tmp, septets, from, from_len);
  } else {
    size_t o = 0;
    if ((typ & 0x70) == 0x10 && o + 1 < from_len) from[o++] = '+';
    for (int d = 0; d < digits && o + 1 < from_len; d++) {
      int nib = (d & 1) ? (a[d / 2] >> 4) : (a[d / 2] & 0x0F);
      if (nib == 0x0F) break;
      if (nib < 15) from[o++] = "0123456789*#abc"[nib];
    }
    from[o] = 0;
  }
  return 0;
}

static bool decodePdu(const uint8_t *b, int len, PduPart *part) {
  memset(part, 0, sizeof(*part));
  part->ref = -1;
  if (len < 2) return false;
  int i = 0;
  int sca = b[i++];
  if (sca > 12 || i + sca > len) return false;
  i += sca;
  if (i >= len) return false;
  int fo = b[i++];
  int mti = fo & 0x03;
  bool udhi = (fo & 0x40) != 0;
  if (mti == 0x01) {
    if (i >= len) return false;
    i++;
  } else if (mti != 0x00) {
    return false;
  }
  if (takeAddr(b, len, &i, part->from, sizeof(part->from)) < 0) return false;
  if (!part->from[0]) {
    part->from[0] = '?';
    part->from[1] = 0;
  }
  if (i + 2 > len) return false;
  i++;
  int dcs = b[i++];
  int alphabet = alphabetOf((uint8_t)dcs);
  if (alphabet == 3) alphabet = 1;
  if (mti == 0x00) {
    if (i + 7 > len) return false;
    i += 7;
  } else {
    int vpf = (fo >> 3) & 0x03;
    int vp = (vpf == 0) ? 0 : (vpf == 2 ? 1 : 7);
    if (i + vp > len) return false;
    i += vp;
  }
  if (i >= len) return false;
  int udl = b[i++];
  int ud_octets = (alphabet == 0) ? (udl * 7 + 7) / 8 : udl;
  if (ud_octets < 0 || i + ud_octets != len) return false;
  const uint8_t *ud = b + i;
  int payload_off = 0;
  int payload_len = (alphabet == 0) ? udl : ud_octets;

  if (udhi) {
    if (ud_octets < 1) return false;
    int udhl = ud[0];
    if (udhl + 1 > ud_octets) return false;
    int p = 1;
    int ref = -1, total = 0, seq = 0;
    while (p + 1 < udhl + 1) {
      int iei = ud[p];
      int iel = ud[p + 1];
      if (p + 2 + iel > udhl + 1) break;
      if (iei == 0x00 && iel == 3) {
        ref = ud[p + 2];
        total = ud[p + 3];
        seq = ud[p + 4];
      } else if (iei == 0x08 && iel == 4) {
        ref = (ud[p + 2] << 8) | ud[p + 3];
        total = ud[p + 4];
        seq = ud[p + 5];
      }
      p += 2 + iel;
    }
    if (alphabet == 0) {
      int header_septets = ((udhl + 1) * 8 + 6) / 7;
      if (header_septets > udl) return false;
      payload_off = header_septets;
      payload_len = udl - header_septets;
    } else {
      payload_off = udhl + 1;
      payload_len = (udl >= payload_off) ? udl - payload_off : 0;
      if (payload_off + payload_len > ud_octets)
        payload_len = ud_octets - payload_off;
    }
    if (total >= 2 && seq >= 1 && seq <= total && ref >= 0) {
      part->ref = ref;
      part->total = total;
      part->seq = seq;
    }
  }

  if (payload_len < 0) payload_len = 0;
  if (payload_len > (int)sizeof(part->data)) payload_len = (int)sizeof(part->data);
  if (alphabet == 0) {
    for (int s = 0; s < payload_len; s++)
      part->data[s] = getSeptet(ud, ud_octets, payload_off + s);
  } else {
    if (payload_off < 0 || payload_off > ud_octets) return false;
    if (payload_off + payload_len > ud_octets)
      payload_len = ud_octets - payload_off;
    if (payload_len > 0) memcpy(part->data, ud + payload_off, (size_t)payload_len);
  }
  part->data_len = payload_len;
  part->alphabet = alphabet;
  return true;
}

static int hexToBin(const char *s, size_t n, uint8_t *out, int max) {
  int produced = 0;
  int hi = -1;
  for (size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    int v;
    if (c >= '0' && c <= '9')
      v = c - '0';
    else if (c >= 'A' && c <= 'F')
      v = c - 'A' + 10;
    else if (c >= 'a' && c <= 'f')
      v = c - 'a' + 10;
    else if (c == ' ' || c == '\t')
      continue;
    else
      break;
    if (hi < 0) {
      hi = v;
    } else {
      if (produced < max) out[produced++] = (uint8_t)((hi << 4) | v);
      hi = -1;
    }
  }
  if (hi >= 0) return 0;
  return produced;
}

static bool indexSkipped(int index, const int *skip, int skip_n) {
  if (!skip || skip_n <= 0) return false;
  for (int i = 0; i < skip_n; i++)
    if (skip[i] == index) return true;
  return false;
}

static bool isDigitChar(char c) { return c >= '0' && c <= '9'; }

static bool isLetterChar(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

// SIM800 в текстовом режиме подменяет '+' в номере на букву.
static void cleanTextSender(char *from) {
  if (!from || !isLetterChar(from[0])) return;
  size_t n = strlen(from);
  if (n < 9) return;
  for (size_t i = 1; i < n; i++)
    if (!isDigitChar(from[i])) return;
  from[0] = '+';
}

static int takeUdh(const uint8_t *b, int len, int *ref, int *total, int *seq) {
  *ref = -1;
  *total = 0;
  *seq = 0;
  if (len < 2) return 0;
  int udhl = b[0];
  if (udhl < 1 || udhl + 1 > len) return 0;
  int p = 1;
  bool found = false;
  int r = -1, t = 0, s = 0;
  while (p + 1 < udhl + 1) {
    int iei = b[p];
    int iel = b[p + 1];
    if (p + 2 + iel > udhl + 1) return 0;
    if (iei == 0x00 && iel == 3) {
      r = b[p + 2];
      t = b[p + 3];
      s = b[p + 4];
      found = true;
    } else if (iei == 0x08 && iel == 4) {
      r = (b[p + 2] << 8) | b[p + 3];
      t = b[p + 4];
      s = b[p + 5];
      found = true;
    }
    p += 2 + iel;
  }
  if (p != udhl + 1 || !found) return 0;
  if (((len - (udhl + 1)) % 2) != 0) return 0;
  *ref = r;
  *total = t;
  *seq = s;
  return udhl + 1;
}

static bool headerHasQuote(const char *tag, const char *eol) {
  for (const char *p = tag; p < eol; p++)
    if (*p == '"') return true;
  return false;
}

static void readQuotedSender(const char *tag, const char *eol, char *from, size_t cap) {
  if (cap == 0) return;
  from[0] = 0;
  int q = 0;
  const char *start = nullptr;
  for (const char *p = tag; p < eol; p++) {
    if (*p != '"') continue;
    q++;
    if (q == 3) start = p + 1;
    if (q == 4 && start) {
      size_t n = (size_t)(p - start);
      if (n >= cap) n = cap - 1;
      memcpy(from, start, n);
      from[n] = 0;
      uint8_t bin[48];
      int bl = hexToBin(from, n, bin, (int)sizeof(bin));
      if (bl >= 2 && (bl % 2) == 0 && (size_t)bl * 2 == n) {
        char tmp[32];
        decodeUtf16Be(bin, bl, tmp, sizeof(tmp));
        if (tmp[0]) {
          strncpy(from, tmp, cap - 1);
          from[cap - 1] = 0;
        }
      }
      cleanTextSender(from);
      return;
    }
  }
}

static bool lineIsHex(const char *s, size_t n, int *digits_out) {
  int digits = 0;
  for (size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))
      digits++;
    else if (c == ' ' || c == '\t')
      continue;
    else
      return false;
  }
  if (digits_out) *digits_out = digits;
  return digits >= 4 && (digits % 2) == 0;
}

static int parseTextParts(const char *resp, const int *skip, int skip_n, PduPart *out,
                          int max_n) {
  int n = 0;
  const char *p = resp ? resp : "";
  while (*p && n < max_n) {
    const char *tag = strstr(p, "+CMGL:");
    if (!tag) break;
    int index = atoi(tag + 6);
    const char *eol = strpbrk(tag, "\r\n");
    if (!eol) break;
    const char *body = eol;
    while (*body == '\r' || *body == '\n') body++;
    if (strncmp(body, "+CMGL:", 6) == 0 || strncmp(body, "OK", 2) == 0 || *body == 0) {
      p = body;
      if (*body == 0) break;
      continue;
    }
    const char *body_end = strpbrk(body, "\r\n");
    if (!body_end) break;
    if (!indexSkipped(index, skip, skip_n)) {
      PduPart part;
      memset(&part, 0, sizeof(part));
      part.index = index;
      part.ref = -1;
      readQuotedSender(tag, eol, part.from, sizeof(part.from));
      if (!part.from[0]) {
        part.from[0] = '?';
        part.from[1] = 0;
      }
      size_t body_len = (size_t)(body_end - body);
      int digits = 0;
      uint8_t bin[400];
      int bl = 0;
      if (lineIsHex(body, body_len, &digits))
        bl = hexToBin(body, body_len, bin, (int)sizeof(bin));
      PduPart as_pdu;
      if (bl > 0 && digits == bl * 2 && decodePdu(bin, bl, &as_pdu)) {
        as_pdu.index = index;
        out[n++] = as_pdu;
        p = body_end;
        continue;
      }
      int ref = -1, total = 0, seq = 0;
      int skip_udh = (bl > 0) ? takeUdh(bin, bl, &ref, &total, &seq) : 0;
      if (skip_udh > 0) {
        part.alphabet = 2;
        part.data_len = bl - skip_udh;
        if (part.data_len > (int)sizeof(part.data)) part.data_len = (int)sizeof(part.data);
        memcpy(part.data, bin + skip_udh, (size_t)part.data_len);
        if (total >= 2 && seq >= 1 && seq <= total && ref >= 0) {
          part.ref = ref;
          part.total = total;
          part.seq = seq;
        }
      } else if (bl >= 4 && (bl % 2) == 0 && digits == bl * 2) {
        part.alphabet = 2;
        part.data_len = bl;
        if (part.data_len > (int)sizeof(part.data)) part.data_len = (int)sizeof(part.data);
        memcpy(part.data, bin, (size_t)part.data_len);
      } else if (!lineIsHex(body, body_len, nullptr)) {
        part.alphabet = 1;
        part.data_len = (int)body_len;
        if (part.data_len > (int)sizeof(part.data)) part.data_len = (int)sizeof(part.data);
        memcpy(part.data, body, (size_t)part.data_len);
      } else {
        p = body_end;
        continue;
      }
      out[n++] = part;
    }
    p = body_end;
  }
  return n;
}

static int parseParts(const char *resp, const int *skip, int skip_n, PduPart *out,
                      int max_n) {
  const char *probe = strstr(resp ? resp : "", "+CMGL:");
  if (probe) {
    const char *eol = strpbrk(probe, "\r\n");
    if (!eol) eol = probe + strlen(probe);
    if (headerHasQuote(probe, eol)) return parseTextParts(resp, skip, skip_n, out, max_n);
  }
  int n = 0;
  const char *p = resp ? resp : "";
  while (*p && n < max_n) {
    const char *tag = strstr(p, "+CMGL:");
    if (!tag) break;
    int index = atoi(tag + 6);
    const char *eol = strpbrk(tag, "\r\n");
    if (!eol) break;
    const char *hex = eol;
    while (*hex == '\r' || *hex == '\n') hex++;
    if (*hex == 0) break;
    const char *hex_end = strpbrk(hex, "\r\n");
    if (!hex_end) break;
    if (!indexSkipped(index, skip, skip_n)) {
      uint8_t bin[256];
      int bl = hexToBin(hex, (size_t)(hex_end - hex), bin, (int)sizeof(bin));
      if (bl > 0 && decodePdu(bin, bl, &out[n])) {
        out[n].index = index;
        n++;
      }
    }
    p = hex_end;
  }
  return n;
}

static bool isConcat(const PduPart &pt) {
  return pt.ref >= 0 && pt.total >= 2 && pt.total <= SMS_PARTS_MAX &&
         pt.seq >= 1 && pt.seq <= pt.total;
}

static bool groupComplete(const PduPart *parts, const PartGroup &g) {
  if (!g.concat) return true;
  bool have[SMS_PARTS_MAX + 1];
  memset(have, 0, sizeof(have));
  for (int i = 0; i < g.n; i++) {
    int seq = parts[g.idx[i]].seq;
    if (seq >= 1 && seq <= g.total && seq <= SMS_PARTS_MAX) have[seq] = true;
  }
  for (int s = 1; s <= g.total; s++)
    if (!have[s]) return false;
  return true;
}

static void emitGroup(const PduPart *parts, const PartGroup &g, SMS *dst) {
  memset(dst, 0, sizeof(*dst));
  int sn = g.n;
  if (sn > SMS_PARTS_MAX) sn = SMS_PARTS_MAX;
  if (sn <= 0) return;
  int sorted[SMS_PARTS_MAX];
  for (int i = 0; i < sn; i++) sorted[i] = g.idx[i];
  for (int i = 0; i < sn; i++) {
    for (int j = i + 1; j < sn; j++) {
      if (parts[sorted[j]].seq < parts[sorted[i]].seq) {
        int t = sorted[i];
        sorted[i] = sorted[j];
        sorted[j] = t;
      }
    }
  }
  dst->index = parts[sorted[0]].index;
  dst->index_n = sn;
  strncpy(dst->from, parts[sorted[0]].from, sizeof(dst->from) - 1);
  for (int i = 0; i < sn; i++) dst->indices[i] = parts[sorted[i]].index;

  uint8_t buf[SMS_PARTS_MAX * 160];
  int bl = 0;
  if (!g.concat) {
    const PduPart &pt = parts[sorted[0]];
    if (pt.data_len > 0) {
      memcpy(buf, pt.data, (size_t)pt.data_len);
      bl = pt.data_len;
    }
  } else {
    bool seen[SMS_PARTS_MAX + 1];
    memset(seen, 0, sizeof(seen));
    for (int i = 0; i < sn; i++) {
      const PduPart &pt = parts[sorted[i]];
      if (pt.seq < 1 || pt.seq > SMS_PARTS_MAX || seen[pt.seq]) continue;
      seen[pt.seq] = true;
      if (pt.data_len <= 0) continue;
      if (bl + pt.data_len > (int)sizeof(buf)) break;
      memcpy(buf + bl, pt.data, (size_t)pt.data_len);
      bl += pt.data_len;
    }
  }
  int alphabet = parts[sorted[0]].alphabet;
  if (alphabet == 0)
    decodeGsm7(buf, bl, dst->text, sizeof(dst->text));
  else if (alphabet == 2)
    decodeUtf16Be(buf, bl, dst->text, sizeof(dst->text));
  else
    decode8bit(buf, bl, dst->text, sizeof(dst->text));
}

static bool sameWait(const ConcatWait &w, const PartGroup &g) {
  return w.used && w.ref == g.ref && w.total == g.total && w.alphabet == g.alphabet &&
         strcmp(w.from, g.from) == 0;
}

int pduCollect(const char *cmgl, SMS *out, int max_out, ConcatWait *waits,
               int max_waits, const int *skip_idx, int skip_n, uint32_t now_ms,
               uint32_t timeout_ms, bool emit_partial) {
  if (!out || max_out <= 0) return 0;
  static PduPart parts[20];
  static PartGroup groups[20];
  int nparts = parseParts(cmgl, skip_idx, skip_n, parts, 20);
  int ng = 0;
  for (int i = 0; i < nparts; i++) {
    const PduPart &pt = parts[i];
    if (!isConcat(pt)) {
      if (ng >= 20) break;
      PartGroup &g = groups[ng++];
      memset(&g, 0, sizeof(g));
      g.idx[0] = i;
      g.n = 1;
      g.alphabet = pt.alphabet;
      continue;
    }
    int found = -1;
    for (int gi = 0; gi < ng; gi++) {
      if (!groups[gi].concat) continue;
      if (groups[gi].ref == pt.ref && groups[gi].total == pt.total &&
          groups[gi].alphabet == pt.alphabet &&
          strcmp(groups[gi].from, pt.from) == 0) {
        found = gi;
        break;
      }
    }
    if (found < 0) {
      if (ng >= 20) continue;
      PartGroup &g = groups[ng++];
      memset(&g, 0, sizeof(g));
      g.concat = true;
      g.ref = pt.ref;
      g.total = pt.total;
      g.alphabet = pt.alphabet;
      strncpy(g.from, pt.from, sizeof(g.from) - 1);
      g.idx[0] = i;
      g.n = 1;
    } else if (groups[found].n < SMS_PARTS_MAX) {
      groups[found].idx[groups[found].n++] = i;
    }
  }

  int wait_n = 0;
  if (waits && max_waits > 0 && !emit_partial) {
    wait_n = max_waits;
    if (wait_n > CONCAT_WAIT_MAX) wait_n = CONCAT_WAIT_MAX;
  }
  bool seen[CONCAT_WAIT_MAX];
  memset(seen, 0, sizeof(seen));
  int out_n = 0;

  for (int gi = 0; gi < ng && out_n < max_out; gi++) {
    PartGroup &g = groups[gi];
    bool complete = groupComplete(parts, g);
    bool do_emit = !g.concat || complete || emit_partial;
    if (!do_emit && wait_n > 0) {
      int slot = -1;
      for (int w = 0; w < wait_n; w++) {
        if (sameWait(waits[w], g)) {
          slot = w;
          break;
        }
      }
      if (slot < 0) {
        for (int w = 0; w < wait_n; w++) {
          if (!waits[w].used) {
            slot = w;
            break;
          }
        }
        if (slot >= 0) {
          memset(&waits[slot], 0, sizeof(waits[slot]));
          waits[slot].used = true;
          waits[slot].ref = g.ref;
          waits[slot].total = g.total;
          waits[slot].alphabet = g.alphabet;
          strncpy(waits[slot].from, g.from, sizeof(waits[slot].from) - 1);
          waits[slot].first_ms = now_ms;
        }
      }
      if (slot >= 0) {
        seen[slot] = true;
        if ((uint32_t)(now_ms - waits[slot].first_ms) >= timeout_ms) {
          do_emit = true;
          seen[slot] = false;
        }
      } else {
        do_emit = true;
      }
    } else if (!do_emit) {
      do_emit = true;
    }
    if (do_emit) emitGroup(parts, g, &out[out_n++]);
  }

  if (wait_n > 0) {
    for (int w = 0; w < wait_n; w++)
      if (!seen[w]) waits[w].used = false;
  }
  return out_n;
}
