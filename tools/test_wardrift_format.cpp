// Host-side proof for the wardrift CSV parser and item serializer.
// Build: g++ -std=c++17 -I../src test_wardrift_format.cpp ../src/WardriftFormat.cpp
#include <cstdio>
#include <cstring>
#include <string>
#include "WardriftFormat.h"

static int failures = 0;

static void expect(bool cond, const char* what) {
  if (!cond) {
    std::printf("FAIL: %s\n", what);
    failures++;
  }
}

static bool parse(const char* s, WdRow* r) {
  return wd_parse_row(s, std::strlen(s), r);
}

int main() {
  const char* wifi =
    "AA:BB:CC:DD:EE:FF,HomeNet,[WPA2-PSK-CCMP][ESS],2026-10-22 7:7:55,6,2437,"
    "-67,40.123456,-75.654321,100.0,5.0,,,WIFI";
  WdRow r{};
  expect(parse(wifi, &r), "parse normal wifi row");
  expect(std::strcmp(r.bssid, "AA:BB:CC:DD:EE:FF") == 0, "bssid");
  expect(std::strcmp(r.ssid, "HomeNet") == 0, "ssid");
  expect(std::strcmp(r.ts, "2026-10-22T07:07:55Z") == 0, "iso padding");
  expect(r.rssi == -67, "rssi");
  expect(r.lat > 40.123 && r.lat < 40.124, "lat");
  expect(r.lon < -75.654 && r.lon > -75.655, "lon");
  expect(!r.ble, "wifi kind");

  char buf[512];
  size_t n = wd_row_json(r, buf, sizeof(buf));
  expect(n > 0, "serialize wifi");
  std::string js(buf, n);
  expect(js.find("\"kind\":\"wifi\"") != std::string::npos, "json kind wifi");
  expect(js.find("\"ssid\":\"HomeNet\"") != std::string::npos, "json ssid");
  expect(js.find("\"source_ts\":\"2026-10-22T07:07:55Z\"") != std::string::npos,
         "json source_ts");
  expect(js.find("\"metadata\":{\"source\":\"wardriver-c5v2\"}") != std::string::npos,
         "json metadata");
  expect(js.front() == '{' && js.back() == '}', "json object shape");

  // Empty SSID: the row that the old sscanf parser silently dropped.
  const char* hidden =
    "11:22:33:44:55:66,, [WPA2-PSK-CCMP],2026-10-22 17:00:01,11,2462,-80,"
    "40.0,-75.0,10,3,,,WIFI";
  WdRow h{};
  expect(parse(hidden, &h), "parse empty-ssid row");
  expect(h.ssid[0] == '\0', "empty ssid preserved");
  n = wd_row_json(h, buf, sizeof(buf));
  expect(n > 0, "serialize empty ssid");
  js.assign(buf, n);
  expect(js.find("\"ssid\":\"\"") != std::string::npos, "empty ssid emitted");
  expect(js.find("\"lat\":40.000000") != std::string::npos, "lat format");

  // BLE row.
  const char* ble =
    "DE:AD:BE:EF:00:01,, [BLE],2026-10-22 17:00:02,0,0,-88,40.5,-75.5,12,4,,0,BLE";
  WdRow b{};
  expect(parse(ble, &b), "parse ble row");
  expect(b.ble, "ble flag");
  n = wd_row_json(b, buf, sizeof(buf));
  js.assign(buf, n);
  expect(js.find("\"kind\":\"bluetooth\"") != std::string::npos, "json kind ble");

  // 11-column layout (legacy / field cards).
  const char* legacy =
    "C8:B5:AD:00:00:01,,[WPA2_PSK],2026-10-16 16:36:19,1,-93,"
    "33.0000000,-96.0000000,217.00,10.00,WIFI";
  WdRow l{};
  expect(parse(legacy, &l), "parse 11-column legacy row");
  expect(l.rssi == -93, "legacy rssi");
  expect(std::strcmp(l.ts, "2026-10-16T16:36:19Z") == 0, "legacy iso");
  expect(l.lat > 32.99 && l.lat < 33.01, "legacy lat");

  // Rejections.
  const char* header =
    "MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude,"
    "CurrentLongitude,AltitudeMeters,AccuracyMeters,RCOIs,MfgrId,Type";
  expect(!parse(header, &r), "reject csv header");
  expect(!parse("AA:BB:CC:DD:EE:FF,NoFix,x,2026-10-22 17:00:03,6,2437,-70,"
                "0.0,0.0,0,0,,,WIFI", &r), "reject 0,0 fix");
  expect(!parse("AA:BB:CC:DD:EE:FF,Bad,x,2026-10-22 17:00:03,6,2437,-70,"
                "91.0,-75.0,0,0,,,WIFI", &r), "reject lat out of range");
  expect(!parse("AA:BB:CC:DD:EE:FF,Short,x,2026-10-22 17:00:03,6,2437,-70", &r),
         "reject truncated row");
  expect(!parse("AA:BB:CC:DD:EE:FF,Comma,Name,x,2026-10-22 17:00:03,6,2437,-70,"
                "40.0,-75.0,0,0,,,WIFI", &r), "reject comma in ssid");

  // JSON escaping.
  WdRow q{};
  expect(parse("AA:BB:CC:DD:EE:FF,He said \"hi\",x,2026-10-22 17:00:03,6,2437,"
               "-70,40.0,-75.0,0,0,,,WIFI", &q), "parse quoted ssid");
  n = wd_row_json(q, buf, sizeof(buf));
  js.assign(buf, n);
  expect(js.find("He said \\\"hi\\\"") != std::string::npos, "escape quote");

  // Small buffer must fail cleanly.
  char tiny[40];
  expect(wd_row_json(r, tiny, sizeof(tiny)) == 0, "small buffer fails");

  // Envelope prefix shape.
  char env[256];
  size_t el = wd_envelope_prefix(env, sizeof(env), "12345678-1234-4123-8123-123456789abc",
                                 1750000000123ULL, "2026-10-24T12:00:00Z");
  expect(el > 0, "envelope prefix fits");
  std::string es(env, el);
  expect(es.find("\"sequence_no\":1750000000123") != std::string::npos, "envelope seq");
  expect(es.find("\"signature\":\"\"") != std::string::npos, "envelope signature");
  expect(es.back() == '[', "envelope opens items array");
  expect(std::string(wd_envelope_suffix()) == "]}", "envelope suffix");

  // Response verdicts: 200 alone is not success.
  expect(wd_classify_response(200, "{\"accepted\":true}") == WD_VERDICT_ACCEPTED,
         "200 accepted");
  expect(wd_classify_response(200, "{\"duplicate\":true}") == WD_VERDICT_ACCEPTED,
         "200 duplicate");
  expect(wd_classify_response(200, "{\"error\":\"x\"}") == WD_VERDICT_FAILED,
         "200 without marker fails");
  expect(wd_classify_response(409, "{\"error\":\"sequence_replay\"}") == WD_VERDICT_SEQUENCE_REPLAY,
         "409 sequence replay");
  expect(wd_classify_response(409, "{\"error\":\"idempotency_key_reused\"}") == WD_VERDICT_IDEMPOTENCY_REUSED,
         "409 idempotency reuse");
  expect(wd_classify_response(401, "{}") == WD_VERDICT_UNAUTHORIZED, "401 unauthorized");
  expect(wd_classify_response(0, "") == WD_VERDICT_RETRY, "transport retry");
  expect(wd_classify_response(400, "bad") == WD_VERDICT_FAILED, "400 fails");

  // Invalid UTF-8 in an SSID must not produce invalid JSON.
  std::string bad = "AA:BB:CC:DD:EE:FF,";
  bad += "\xC0";
  bad += "bad,x,2026-10-24 12:00:00,6,2437,-70,40.0,-75.0,0,0,,,WIFI";
  WdRow u{};
  expect(parse(bad.c_str(), &u), "parse invalid-utf8 ssid");
  expect(std::strchr(u.ssid, '?') != nullptr, "invalid byte replaced");
  n = wd_row_json(u, buf, sizeof(buf));
  expect(n > 0, "serialize sanitized ssid");
  for (size_t i = 0; i < n; i++)
    expect((unsigned char)buf[i] < 0x80, "json output is ascii-safe");

  if (failures == 0) std::printf("PASS: wardrift format\n");
  return failures == 0 ? 0 : 1;
}
