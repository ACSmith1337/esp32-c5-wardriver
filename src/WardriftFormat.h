#pragma once
// wardrift.net ingest item formatting.
//
// Pure C++ (no Arduino) so the exact parser/serializer used by the firmware
// is unit-tested on the host. The firmware's log format is the stock WiGLE
// CSV written by JCMK v2.3.x:
//
//   MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,
//   CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,
//   RCOIs,MfgrId,Type
//
// Commas inside an SSID are replaced with '_' by the firmware before the row
// is written, so a strict 14-field split is correct. Empty fields ARE valid
// (hidden SSIDs are common) and must not be dropped.

#include <stddef.h>
#include <stdint.h>

#define WD_BSSID_MAX 18
#define WD_SSID_MAX  96
#define WD_TS_MAX    21   // "YYYY-MM-DDTHH:MM:SSZ" + NUL

struct WdRow {
  char    bssid[WD_BSSID_MAX];
  char    ssid[WD_SSID_MAX];
  char    ts[WD_TS_MAX];
  double  lat;
  double  lon;
  int32_t rssi;
  bool    ble;
};

// Parse one log line (without its trailing CR/LF). Returns false for the
// header, truncated rows, malformed fields, or rows without a usable fix.
bool wd_parse_row(const char* line, size_t len, WdRow* out);

// Serialize one row as a wardrift.net `items[]` object. Returns the number of
// bytes written (excluding NUL), or 0 when the output buffer is too small.
size_t wd_row_json(const WdRow& row, char* out, size_t cap);

// Envelope prefix: {"idempotency_key":"...","sequence_no":N,"device_ts":"...",
// "signature":"","items":[  -- the caller appends comma-separated items and
// then wd_envelope_suffix(). Returns 0 if the buffer is too small.
size_t wd_envelope_prefix(char* out, size_t cap, const char* uuid,
                          uint64_t seq, const char* device_ts);
const char* wd_envelope_suffix();

enum WdVerdict {
  WD_VERDICT_ACCEPTED = 0,
  WD_VERDICT_SEQUENCE_REPLAY,
  WD_VERDICT_IDEMPOTENCY_REUSED,
  WD_VERDICT_UNAUTHORIZED,
  WD_VERDICT_RETRY,
  WD_VERDICT_FAILED
};

// Classify an ingest response. Status alone is not enough: wardrift.net
// answers 200 with the real verdict in the JSON body.
WdVerdict wd_classify_response(int status, const char* body);

// "2026-10-22 7:7:55" -> "2026-10-22T07:07:55Z". Returns false on garbage.
bool wd_iso_from_gps(const char* gps_dt, char out[WD_TS_MAX]);
