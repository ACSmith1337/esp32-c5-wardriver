#include "WardriftFormat.h"

#include <math.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

struct Field {
  const char* p;
  size_t      n;
};

// Split on commas. Returns the field count, or -1 when there are more than
// `max` fields (the caller only cares about exact 14).
int split_fields(const char* line, size_t len, Field* fields, int max) {
  int count = 0;
  size_t start = 0;
  for (size_t i = 0; i <= len; i++) {
    if (i == len || line[i] == ',') {
      if (count >= max) return -1;
      fields[count].p = line + start;
      fields[count].n = i - start;
      count++;
      start = i + 1;
    }
  }
  return count;
}

bool copy_field(const Field& f, char* dst, size_t cap) {
  if (f.n >= cap) return false;
  memcpy(dst, f.p, f.n);
  dst[f.n] = '\0';
  return true;
}

// SSIDs are arbitrary bytes on air. JSON must be valid UTF-8, so replace
// malformed sequences with '?' while preserving valid multi-byte text.
void sanitize_utf8(char* s) {
  const unsigned char* p = (const unsigned char*)s;
  unsigned char* out = (unsigned char*)s;
  while (*p) {
    unsigned char c = *p;
    if (c < 0x80) {
      *out++ = c;
      p++;
      continue;
    }
    int len = 0;
    if (c >= 0xC2 && c <= 0xDF) len = 2;
    else if (c >= 0xE0 && c <= 0xEF) len = 3;
    else if (c >= 0xF0 && c <= 0xF4) len = 4;

    bool valid = (len > 0);
    for (int i = 1; valid && i < len; i++) {
      if ((p[i] & 0xC0) != 0x80) valid = false;
    }
    if (valid) {
      for (int i = 0; i < len; i++) *out++ = *p++;
    } else {
      *out++ = '?';
      p++;
    }
  }
  *out = '\0';
}

int field_to_int(const Field& f, bool* ok) {
  char tmp[16];
  if (f.n == 0 || f.n >= sizeof(tmp)) { *ok = false; return 0; }
  memcpy(tmp, f.p, f.n);
  tmp[f.n] = '\0';
  char* end = nullptr;
  long v = strtol(tmp, &end, 10);
  *ok = (end && *end == '\0');
  return (int)v;
}

double field_to_double(const Field& f, bool* ok) {
  char tmp[32];
  if (f.n == 0 || f.n >= sizeof(tmp)) { *ok = false; return 0.0; }
  memcpy(tmp, f.p, f.n);
  tmp[f.n] = '\0';
  char* end = nullptr;
  double v = strtod(tmp, &end);
  *ok = (end && *end == '\0') && isfinite(v);
  return v;
}

size_t append(char* out, size_t cap, size_t off, const char* s) {
  size_t n = strlen(s);
  if (off + n >= cap) return 0;
  memcpy(out + off, s, n);
  out[off + n] = '\0';
  return off + n;
}

size_t append_json_string(char* out, size_t cap, size_t off, const char* s) {
  static const char* hex = "0123456789abcdef";
  for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
    unsigned char c = *p;
    if (c == '"' || c == '\\') {
      if (off + 2 >= cap) return 0;
      out[off++] = '\\';
      out[off++] = (char)c;
    } else if (c < 0x20) {
      if (off + 6 >= cap) return 0;
      out[off++] = '\\';
      out[off++] = 'u';
      out[off++] = '0';
      out[off++] = '0';
      out[off++] = hex[(c >> 4) & 0xF];
      out[off++] = hex[c & 0xF];
    } else {
      if (off + 1 >= cap) return 0;
      out[off++] = (char)c;
    }
  }
  out[off] = '\0';
  return off;
}

}  // namespace

bool wd_iso_from_gps(const char* gps_dt, char out[WD_TS_MAX]) {
  if (!gps_dt || !*gps_dt) return false;
  int y, mo, d, h, mi, se;
  if (sscanf(gps_dt, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6)
    return false;
  if (y < 2020 || y > 2100 || mo < 1 || mo > 12 || d < 1 || d > 31 ||
      h < 0 || h > 23 || mi < 0 || mi > 59 || se < 0 || se > 60)
    return false;
  snprintf(out, WD_TS_MAX, "%04d-%02d-%02dT%02d:%02d:%02dZ",
           y, mo, d, h, mi, se);
  return true;
}

bool wd_parse_row(const char* line, size_t len, WdRow* out) {
  if (!line || !out || len == 0) return false;
  while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) len--;

  // The stock firmware has emitted two layouts:
  //   11 columns: MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,Lat,Lon,Alt,Acc,Type
  //   14 columns: + Frequency, RCOIs, MfgrId before Type
  // Field logs on the card may be either, so accept both.
  Field f[14];
  int n = split_fields(line, len, f, 14);
  int rssi_i, lat_i, lon_i, type_i;
  if (n == 14) {
    rssi_i = 6; lat_i = 7; lon_i = 8; type_i = 13;
  } else if (n == 11) {
    rssi_i = 5; lat_i = 6; lon_i = 7; type_i = 10;
  } else {
    return false;
  }

  memset(out, 0, sizeof(*out));

  if (!copy_field(f[0], out->bssid, sizeof(out->bssid))) return false;
  // BSSID must be a colon-separated hex MAC, not the CSV header ("MAC").
  if (strlen(out->bssid) != 17) return false;
  for (int i = 0; i < 17; i++) {
    if (i % 3 == 2) {
      if (out->bssid[i] != ':') return false;
    } else if (!isxdigit((unsigned char)out->bssid[i])) {
      return false;
    }
  }

  if (!copy_field(f[1], out->ssid, sizeof(out->ssid))) return false;
  sanitize_utf8(out->ssid);

  char dt[32];
  if (!copy_field(f[3], dt, sizeof(dt))) return false;
  if (!wd_iso_from_gps(dt, out->ts)) return false;

  bool ok = false;
  out->rssi = field_to_int(f[rssi_i], &ok);
  if (!ok || out->rssi < -127 || out->rssi > 10) return false;

  out->lat = field_to_double(f[lat_i], &ok);
  if (!ok || out->lat < -90.0 || out->lat > 90.0) return false;
  out->lon = field_to_double(f[lon_i], &ok);
  if (!ok || out->lon < -180.0 || out->lon > 180.0) return false;
  // A row with no usable fix is not ingestable.
  if (out->lat == 0.0 && out->lon == 0.0) return false;

  char kind[8];
  if (!copy_field(f[type_i], kind, sizeof(kind))) return false;
  if (strcmp(kind, "BLE") == 0) {
    out->ble = true;
  } else if (strcmp(kind, "WIFI") == 0 || strcmp(kind, "W") == 0) {
    out->ble = false;
  } else {
    return false;
  }

  return true;
}

size_t wd_row_json(const WdRow& row, char* out, size_t cap) {
  if (!out || cap < 32) return 0;
  size_t off = 0;

  off = append(out, cap, off, "{\"ts\":\"");
  if (!off) return 0;
  off = append(out, cap, off, row.ts);
  if (!off) return 0;
  off = append(out, cap, off, "\",\"lat\":");
  if (!off) return 0;

  char num[40];
  snprintf(num, sizeof(num), "%.6f", row.lat);
  off = append(out, cap, off, num);
  if (!off) return 0;
  off = append(out, cap, off, ",\"lon\":");
  if (!off) return 0;
  snprintf(num, sizeof(num), "%.6f", row.lon);
  off = append(out, cap, off, num);
  if (!off) return 0;

  off = append(out, cap, off, row.ble
    ? ",\"kind\":\"bluetooth\",\"bssid\":\""
    : ",\"kind\":\"wifi\",\"bssid\":\"");
  if (!off) return 0;
  off = append(out, cap, off, row.bssid);
  if (!off) return 0;

  off = append(out, cap, off, "\",\"source_ts\":\"");
  if (!off) return 0;
  off = append(out, cap, off, row.ts);
  if (!off) return 0;

  snprintf(num, sizeof(num), "\",\"rssi\":%ld,\"ssid\":\"", (long)row.rssi);
  off = append(out, cap, off, num);
  if (!off) return 0;
  off = append_json_string(out, cap, off, row.ssid);
  if (!off) return 0;

  off = append(out, cap, off, "\",\"metadata\":{\"source\":\"wardriver-c5v2\"}}");
  return off;
}

size_t wd_envelope_prefix(char* out, size_t cap, const char* uuid,
                          uint64_t seq, const char* device_ts) {
  if (!out || !uuid || !device_ts) return 0;
  int n = snprintf(out, cap,
                   "{\"idempotency_key\":\"%s\",\"sequence_no\":%llu,"
                   "\"device_ts\":\"%s\",\"signature\":\"\",\"items\":[",
                   uuid, (unsigned long long)seq, device_ts);
  if (n < 0 || (size_t)n >= cap) return 0;
  return (size_t)n;
}

const char* wd_envelope_suffix() {
  return "]}";
}

WdVerdict wd_classify_response(int status, const char* body) {
  const char* b = body ? body : "";
  if (status == 200) {
    if (strstr(b, "\"accepted\":true") || strstr(b, "\"duplicate\":true"))
      return WD_VERDICT_ACCEPTED;
    return WD_VERDICT_FAILED;
  }
  if (status == 409) {
    if (strstr(b, "sequence_replay")) return WD_VERDICT_SEQUENCE_REPLAY;
    if (strstr(b, "idempotency_key_reused")) return WD_VERDICT_IDEMPOTENCY_REUSED;
    return WD_VERDICT_FAILED;
  }
  if (status == 401) return WD_VERDICT_UNAUTHORIZED;
  if (status == 0 || status >= 500) return WD_VERDICT_RETRY;
  return WD_VERDICT_FAILED;
}
