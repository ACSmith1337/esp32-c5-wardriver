#include "WardriftClient.h"

#include "WardriftFormat.h"
#include "WardriftTrust.h"
#include "logger.h"
#include "display.h"

#include <SD.h>
#include <SPIFFS.h>
#include <WiFiClientSecure.h>
#include <esp_random.h>
#include <time.h>

extern Display display;

namespace {

constexpr const char* WD_HOST = "wardrift.net";
constexpr uint16_t WD_PORT = 443;
constexpr const char* WD_PATH = "/v1/ingest/signals";
constexpr const char* WD_SEQ_FILE = "/wd_seq";
constexpr const char* WD_RESUME_FILE = "/wd_resume";
constexpr uint16_t WD_BATCH_ROWS = 150;
constexpr size_t WD_ITEMS_RESERVE = 34 * 1024;
constexpr size_t WD_RESPONSE_CAP = 8192;
constexpr uint64_t WD_SEQ_CEILING = 9000000000000000000ULL;
constexpr uint32_t WD_CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t WD_IO_TIMEOUT_MS = 12000;
constexpr bool WD_DUMP_FIRST_BATCH = false;  // debug: /wd_last.json

bool writeAll(WiFiClient& c, const uint8_t* p, size_t len, uint32_t budgetMs) {
  size_t off = 0;
  uint32_t t0 = millis();
  while (off < len) {
    if (!c.connected()) return false;
    size_t chunk = (len - off > 1024) ? 1024 : (len - off);
    int n = c.write(p + off, chunk);
    if (n <= 0) {
      if (millis() - t0 > budgetMs) return false;
      delay(20);
      continue;
    }
    off += (size_t)n;
  }
  return true;
}

String uuid4() {
  static const char* hex = "0123456789abcdef";
  char h[33];
  for (int i = 0; i < 32; i++) h[i] = hex[esp_random() & 0xF];
  h[12] = '4';                              // version 4
  h[16] = "89ab"[esp_random() & 0x3];       // RFC 4122 variant
  h[32] = '\0';
  char out[37];
  snprintf(out, sizeof(out), "%.8s-%.4s-%.4s-%.4s-%.12s",
           h, h + 8, h + 12, h + 16, h + 20);
  return String(out);
}

String isoNow() {
  time_t t = time(nullptr);
  struct tm tmv;
  gmtime_r(&t, &tmv);
  char out[24];
  snprintf(out, sizeof(out), "%04d-%02d-%02dT%02d:%02d:%02dZ",
           tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
           tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  return String(out);
}

uint64_t nowMs() {
  time_t t = time(nullptr);
  if (t < 1700000000) return 0;
  return (uint64_t)t * 1000ULL + (uint64_t)(millis() % 1000);
}

String leafName(const String& path) {
  int s = path.lastIndexOf('/');
  return (s >= 0) ? path.substring(s + 1) : path;
}

bool loadResume(const String& filePath, uint32_t fileSize, uint32_t& offset) {
  offset = 0;
  File f = SPIFFS.open(WD_RESUME_FILE, FILE_READ);
  if (!f) return false;
  String line = f.readStringUntil('\n');
  f.close();
  char name[33];
  unsigned int size = 0, off = 0;
  if (sscanf(line.c_str(), "%32[^,],%u,%u", name, &size, &off) != 3) return false;
  if (String(name) != leafName(filePath)) return false;
  if (size != fileSize) return false;
  if (off == 0 || off > fileSize) return false;
  offset = off;
  return true;
}

void saveResume(const String& filePath, uint32_t fileSize, uint32_t offset) {
  File f = SPIFFS.open(WD_RESUME_FILE, FILE_WRITE);
  if (!f) return;
  f.print(leafName(filePath));
  f.print(',');
  f.print(fileSize);
  f.print(',');
  f.println(offset);
  f.close();
}

void clearResume() {
  SPIFFS.remove(WD_RESUME_FILE);
}

// Read status line + headers + body with a Content-Length early exit.
// Returns the numeric status (0 on transport failure).
int readResponse(WiFiClientSecure& c, String& body) {
  String headers;
  headers.reserve(512);
  uint32_t t0 = millis();
  int header_end = -1;
  while (millis() - t0 < WD_IO_TIMEOUT_MS) {
    while (c.available()) {
      char ch = (char)c.read();
      headers += ch;
      if (headers.length() >= 4 &&
          headers.endsWith("\r\n\r\n")) {
        header_end = headers.length();
        break;
      }
      if (headers.length() > 2048) break;
    }
    if (header_end > 0) break;
    if (!c.connected() && !c.available()) break;
    delay(5);
  }
  if (headers.length() < 12) return 0;

  int status = 0;
  if (sscanf(headers.c_str(), "HTTP/%*d.%*d %d", &status) != 1 &&
      sscanf(headers.c_str(), "HTTP/1.1 %d", &status) != 1)
    return 0;

  long content_length = -1;
  int cl = headers.indexOf("Content-Length:");
  if (cl < 0) cl = headers.indexOf("content-length:");
  if (cl >= 0) content_length = headers.substring(cl + 15).toInt();

  body.reserve(content_length > 0 && content_length < (long)WD_RESPONSE_CAP
                 ? (size_t)content_length + 8 : 256);
  t0 = millis();
  while (millis() - t0 < WD_IO_TIMEOUT_MS) {
    while (c.available()) {
      body += (char)c.read();
      if (body.length() >= WD_RESPONSE_CAP) break;
    }
    if (content_length >= 0 && body.length() >= (size_t)content_length) break;
    if (!c.connected() && !c.available()) break;
    if (body.length() >= WD_RESPONSE_CAP) break;
    delay(5);
  }
  return status;
}

}  // namespace

uint64_t wardriftLoadSequence() {
  File f = SPIFFS.open(WD_SEQ_FILE, FILE_READ);
  if (!f) return 0;
  uint64_t v = 0;
  for (int i = 0; i < 8; i++) {
    int c = f.read();
    if (c < 0) { v = 0; break; }
    v |= (uint64_t)(uint8_t)c << (8 * i);
  }
  f.close();
  // Erased flash reads 0xFF: reject the all-ones poison value outright.
  if (v == 0 || v > WD_SEQ_CEILING) return 0;
  return v;
}

void wardriftSaveSequence(uint64_t seq) {
  if (seq > WD_SEQ_CEILING) return;
  File f = SPIFFS.open(WD_SEQ_FILE, FILE_WRITE);
  if (!f) {
    Logger::log(WARN_MSG, "[WARDRIFT] Could not persist sequence");
    return;
  }
  bool ok = true;
  for (int i = 0; i < 8; i++) {
    if (f.write((uint8_t)((seq >> (8 * i)) & 0xFF)) != 1) ok = false;
  }
  f.close();
  if (!ok) Logger::log(WARN_MSG, "[WARDRIFT] Short write persisting sequence");
}

bool WardriftClient::ensureClock() {
  if (time(nullptr) > 1700000000) return true;
  Logger::log(STD_MSG, "[WARDRIFT] Waiting for NTP...");
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  uint32_t t0 = millis();
  while (millis() - t0 < 12000) {
    if (time(nullptr) > 1700000000) return true;
    delay(100);
  }
  last_error_ = "NTP not synced";
  Logger::log(WARN_MSG, "[WARDRIFT] NTP not synced; refusing epoch-0 upload");
  return false;
}

bool WardriftClient::sendBatch(const String& apiKey, const String& items,
                               uint64_t seq, const String& deviceTs,
                               String& error, int& httpStatus) {
  error = "";
  httpStatus = 0;

  String prefix;
  prefix.reserve(256);
  char prefix_buf[256];
  size_t prefix_len = wd_envelope_prefix(prefix_buf, sizeof(prefix_buf),
                                         uuid4().c_str(), seq, deviceTs.c_str());
  if (prefix_len == 0) {
    error = "envelope";
    return false;
  }
  prefix = prefix_buf;

  const char* suffix = wd_envelope_suffix();
  size_t total = prefix.length() + items.length() + 2;

  if (WD_DUMP_FIRST_BATCH) {
    static bool dumped = false;
    if (!dumped) {
      dumped = true;
      File d = SD.open("/wd_last.json", FILE_WRITE);
      if (d) {
        d.print(prefix);
        d.print(items);
        d.print(suffix);
        d.close();
        Logger::log(STD_MSG, "[WARDRIFT] dumped first batch to /wd_last.json len=" + String((unsigned long)total));
      }
    }
  }

  WiFiClientSecure client;
  client.setCACert(WARDRIFT_CA_PEM);
  client.setTimeout(WD_CONNECT_TIMEOUT_MS);
  if (!client.connect(WD_HOST, WD_PORT)) {
    error = "connect failed";
    return false;
  }

  String head;
  head.reserve(320);
  head += "POST ";
  head += WD_PATH;
  head += " HTTP/1.1\r\nHost: ";
  head += WD_HOST;
  head += "\r\nUser-Agent: wardriver-c5v2/2.0\r\nAccept: application/json\r\n";
  head += "Content-Type: application/json\r\nX-API-Key: ";
  head += apiKey;
  head += "\r\nConnection: close\r\nContent-Length: ";
  head += String((unsigned long)total);
  head += "\r\n\r\n";

  bool sent = writeAll(client, (const uint8_t*)head.c_str(), head.length(), WD_IO_TIMEOUT_MS) &&
              writeAll(client, (const uint8_t*)prefix.c_str(), prefix.length(), WD_IO_TIMEOUT_MS) &&
              writeAll(client, (const uint8_t*)items.c_str(), items.length(), WD_IO_TIMEOUT_MS) &&
              writeAll(client, (const uint8_t*)suffix, 2, WD_IO_TIMEOUT_MS);
  if (!sent) {
    client.stop();
    error = "short write";
    return false;
  }

  String body;
  int status = readResponse(client, body);
  client.stop();
  httpStatus = status;

  WdVerdict verdict = wd_classify_response(status, body.c_str());
  switch (verdict) {
    case WD_VERDICT_ACCEPTED:
      return true;
    case WD_VERDICT_SEQUENCE_REPLAY:
      error = "sequence_replay";
      return false;
    case WD_VERDICT_IDEMPOTENCY_REUSED:
      error = "idempotency_key_reused";
      return false;
    case WD_VERDICT_UNAUTHORIZED:
      error = "unauthorized";
      return false;
    case WD_VERDICT_RETRY:
      error = (status == 0) ? "no response" : "HTTP " + String(status);
      return false;
    case WD_VERDICT_FAILED:
    default:
      break;
  }

  if (status == 200) {
    error = "200 without accepted marker";
  } else {
    error = "HTTP " + String(status);
    String excerpt = body.substring(0, 400);
    excerpt.replace("\r", " ");
    excerpt.replace("\n", " ");
    if (!excerpt.isEmpty()) error += ": " + excerpt;
  }
  return false;
}

bool WardriftClient::uploadFile(const String& filePath, const String& apiKey, Stats* stats) {
  last_error_ = "";
  Stats local;
  Stats& st = stats ? *stats : local;
  st = Stats();

  if (apiKey.isEmpty()) {
    last_error_ = "no API key";
    Logger::log(WARN_MSG, "[WARDRIFT] No API key configured");
    return false;
  }
  if (!SD.exists(filePath)) {
    last_error_ = "file not found";
    Logger::log(WARN_MSG, "[WARDRIFT] File not found: " + filePath);
    return false;
  }
  if (!ensureClock()) return false;

  File f = SD.open(filePath, FILE_READ);
  if (!f) {
    last_error_ = "open failed";
    Logger::log(WARN_MSG, "[WARDRIFT] Could not open " + filePath);
    return false;
  }

  uint32_t totalSize = (uint32_t)f.size();
  uint32_t startOffset = 0;
  if (loadResume(filePath, totalSize, startOffset)) {
    if (!f.seek(startOffset)) {
      startOffset = 0;
    } else {
      Logger::log(STD_MSG, "[WARDRIFT] Resuming at byte " + String(startOffset));
    }
  }

  uint64_t seq = wardriftLoadSequence();
  if (seq == 0) seq = nowMs();
  if (seq == 0) { f.close(); last_error_ = "clock"; return false; }

  display.clearScreen();
  display.drawCenteredText("Wardrift upload", true);

  String items;
  items.reserve(WD_ITEMS_RESERVE);
  String batchTs;
  uint16_t batchRows = 0;
  bool ok = true;

  auto flush = [&]() -> bool {
    if (batchRows == 0) return true;
    // device_ts must describe the data, not the upload moment. Using wall
    // clock here makes every batch look like it happened at upload time and
    // breaks server-side route grouping.
    String deviceTs = batchTs.isEmpty() ? isoNow() : batchTs;
    String err;
    int status = 0;
    bool accepted = false;
    for (int attempt = 0; attempt < 3; attempt++) {
      uint64_t useq = seq + 1;
      if (useq > WD_SEQ_CEILING) useq = WD_SEQ_CEILING;
      if (sendBatch(apiKey, items, useq, deviceTs, err, status)) {
        seq = useq;
        wardriftSaveSequence(seq);
        st.sent += batchRows;
        st.batches++;
        saveResume(filePath, totalSize, (uint32_t)f.position());
        accepted = true;
        break;
      }
      if (err == "sequence_replay") {
        // Move above the server watermark we know about. Gaps are legal.
        uint64_t jump = seq + 1000;
        uint64_t wall = nowMs();
        if (wall + 1000 > jump) jump = wall + 1000;
        seq = (jump > WD_SEQ_CEILING) ? WD_SEQ_CEILING : jump;
        Logger::log(WARN_MSG, "[WARDRIFT] sequence replay; retry at " +
                    String((unsigned long long)seq));
        continue;
      }
      if (err == "idempotency_key_reused") {
        // A fresh UUID is generated on every sendBatch call.
        continue;
      }
      if (status == 0 || status >= 500) {
        delay(400);
        continue;
      }
      break;  // auth / schema / other 4xx: retrying the same batch won't help
    }
    if (!accepted) {
      last_error_ = err.isEmpty() ? "batch failed" : err;
      if (last_error_ == "sequence_replay")
        last_error_ = "sequence_replay: device key exhausted, mint a new key";
      Logger::log(WARN_MSG, "[WARDRIFT] Batch failed: " + last_error_);
      return false;
    }
    items = "";
    batchTs = "";
    batchRows = 0;
    return true;
  };

  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (line.length() == 0) continue;
    if (line.length() > 1024) { st.skipped++; continue; }
    if (line.startsWith("WigleWifi") || line.startsWith("MAC,")) continue;

    WdRow row;
    if (!wd_parse_row(line.c_str(), line.length(), &row)) {
      st.skipped++;
      continue;
    }
    char item[512];
    size_t n = wd_row_json(row, item, sizeof(item));
    if (n == 0) {
      st.skipped++;
      continue;
    }
    if (batchRows > 0) items += ',';
    items += item;
    batchTs = row.ts;
    batchRows++;

    if (batchRows >= WD_BATCH_ROWS) {
      if (!flush()) { ok = false; break; }
    }
  }
  f.close();

  if (ok && batchRows > 0) ok = flush();

  // An empty or all-unparseable log is not a successful upload. The old
  // behavior wrote a .wardrift sidecar for a headers-only file and reported
  // sent=0 as success.
  if (ok && st.sent == 0 && startOffset == 0) {
    ok = false;
    last_error_ = "no ingestable rows";
    Logger::log(WARN_MSG, "[WARDRIFT] No ingestable rows in " + filePath +
                " (skipped=" + String(st.skipped) + ")");
  }

  if (ok) {
    clearResume();
    Logger::log(GUD_MSG, "[WARDRIFT] Uploaded " + filePath +
                " sent=" + String(st.sent) + " skipped=" + String(st.skipped) +
                " batches=" + String(st.batches));
    display.clearScreen();
    display.drawCenteredText("Wardrift OK", true);
  } else {
    Logger::log(WARN_MSG, "[WARDRIFT] Upload failed: " + last_error_);
    display.clearScreen();
    display.drawCenteredText("Wardrift failed", true);
  }
  return ok;
}
