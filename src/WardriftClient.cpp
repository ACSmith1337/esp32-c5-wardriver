#include "WardriftClient.h"

#include "WardriftFormat.h"
#include "WardriftTrust.h"
#include "logger.h"
#include "display.h"

#include <SD.h>
#include <SPIFFS.h>
#include <WiFiClientSecure.h>
#include "mbedtls/sha256.h"
#include <time.h>

extern Display display;

namespace {

constexpr const char* WD_HOST = "wardrift.net";
constexpr uint16_t WD_PORT = 443;
constexpr const char* WD_PATH = "/v1/ingest/signals";
constexpr const char* WD_SIGNIN_PATH = "/v1/auth/sign-in/username";
constexpr const char* WD_LOGS_PATH = "/v1/wardrive/logs";
constexpr const char* WD_SEQ_FILE = "/wd_seq";
constexpr const char* WD_RESUME_FILE = "/wd_resume";
constexpr const char* WD_SESSION_FILE = "/wd_session";
constexpr uint16_t WD_BATCH_ROWS = 250;
constexpr size_t WD_ITEMS_RESERVE = 56 * 1024;
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

// Deterministic UUIDv5-style idempotency key. The same batch content and key
// always produce the same id, so a retry after a lost response is recognized
// by the server instead of counted twice.
String batchIdem(const String& apiKey, const String& items) {
  static const char* hex = "0123456789abcdef";
  uint8_t h[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, (const unsigned char*)apiKey.c_str(), apiKey.length());
  mbedtls_sha256_update(&ctx, (const unsigned char*)items.c_str(), items.length());
  mbedtls_sha256_finish(&ctx, h);
  mbedtls_sha256_free(&ctx);

  h[6] = (h[6] & 0x0F) | 0x50;  // version 5
  h[8] = (h[8] & 0x3F) | 0x80;  // RFC 4122 variant

  char out[37];
  int p = 0;
  for (int i = 0; i < 16; i++) {
    if (i == 4 || i == 6 || i == 8 || i == 10) out[p++] = '-';
    out[p++] = hex[(h[i] >> 4) & 0xF];
    out[p++] = hex[h[i] & 0xF];
  }
  out[p] = '\0';
  return String(out);
}

String jsonStringField(const String& body, const char* key) {
  String needle = "\"" + String(key) + "\":\"";
  int i = body.indexOf(needle);
  if (i < 0) return "";
  i += needle.length();
  int j = body.indexOf('"', i);
  if (j < 0) return "";
  return body.substring(i, j);
}

long jsonIntField(const String& body, const char* key) {
  String needle = "\"" + String(key) + "\":";
  int i = body.indexOf(needle);
  if (i < 0) return -1;
  i += needle.length();
  int j = i;
  while (j < (int)body.length() && (body[j] == '-' || (body[j] >= '0' && body[j] <= '9'))) j++;
  return body.substring(i, j).toInt();
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

String jsonEscape(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if ((uint8_t)c < 0x20) {
          char esc[7];
          snprintf(esc, sizeof(esc), "\\u%04x", (uint8_t)c);
          out += esc;
        } else {
          out += c;
        }
    }
  }
  return out;
}

bool loadSessionToken(String& token) {
  token = "";
  File f = SPIFFS.open(WD_SESSION_FILE, FILE_READ);
  if (!f) return false;
  token = f.readStringUntil('\n');
  f.close();
  token.trim();
  return !token.isEmpty();
}

void saveSessionToken(const String& token) {
  File f = SPIFFS.open(WD_SESSION_FILE, FILE_WRITE);
  if (!f) return;
  f.print(token);
  f.close();
}

}  // namespace

bool wardriftSessionCached() {
  String token;
  return loadSessionToken(token);
}

void wardriftClearSession() {
  SPIFFS.remove(WD_SESSION_FILE);
}

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
                               const String& idem, uint64_t seq,
                               const String& deviceTs,
                               String& error, int& httpStatus) {
  error = "";
  httpStatus = 0;

  String prefix;
  prefix.reserve(256);
  char prefix_buf[256];
  size_t prefix_len = wd_envelope_prefix(prefix_buf, sizeof(prefix_buf),
                                         idem.c_str(), seq, deviceTs.c_str());
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

  if (status == 200) {
    String batchId = jsonStringField(body, "batch_id");
    long itemCount = jsonIntField(body, "item_count");
    long awardedExp = jsonIntField(body, "awarded_exp");
    bool duplicate = body.indexOf("\"duplicate\":true") >= 0 ||
                     body.indexOf("\"duplicate\": true") >= 0;
    Logger::log(GUD_MSG, "[WARDRIFT] batch=" + batchId +
                " items=" + String(itemCount) +
                " duplicate=" + String(duplicate ? "yes" : "no") +
                " exp=" + String(awardedExp));
  }

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

bool WardriftClient::signIn(const String& username, const String& password, String& token) {
  token = "";

  WiFiClientSecure client;
  client.setCACert(WARDRIFT_CA_PEM);
  client.setTimeout(WD_CONNECT_TIMEOUT_MS);
  if (!client.connect(WD_HOST, WD_PORT)) {
    last_error_ = "sign-in connect failed";
    return false;
  }

  String body;
  body.reserve(192);
  body = "{\"username\":\"";
  body += jsonEscape(username);
  body += "\",\"password\":\"";
  body += jsonEscape(password);
  body += "\",\"remember_me\":true}";

  String head;
  head.reserve(320);
  head += "POST ";
  head += WD_SIGNIN_PATH;
  head += " HTTP/1.1\r\nHost: ";
  head += WD_HOST;
  head += "\r\nUser-Agent: wardriver-c5v2/2.0\r\nAccept: application/json\r\n";
  head += "Content-Type: application/json\r\nConnection: close\r\nContent-Length: ";
  head += String((unsigned long)body.length());
  head += "\r\n\r\n";

  bool sent = writeAll(client, (const uint8_t*)head.c_str(), head.length(), WD_IO_TIMEOUT_MS) &&
              writeAll(client, (const uint8_t*)body.c_str(), body.length(), WD_IO_TIMEOUT_MS);
  if (!sent) {
    client.stop();
    last_error_ = "sign-in short write";
    return false;
  }

  String response;
  int status = readResponse(client, response);
  client.stop();

  if (status == 200) {
    token = jsonStringField(response, "token");
    if (token.isEmpty()) {
      last_error_ = "sign-in returned no token";
      return false;
    }
    Logger::log(GUD_MSG, "[WARDRIFT] Signed in");
    return true;
  }

  last_error_ = (status == 401 || status == 403) ? "sign-in rejected"
                                                : "sign-in HTTP " + String(status);
  Logger::log(WARN_MSG, "[WARDRIFT] " + last_error_);
  return false;
}

bool WardriftClient::sendRoute(const String& token, const String& filename, File& file,
                               uint32_t fileSize, bool publicRoute, String& response,
                               int& httpStatus) {
  response = "";
  httpStatus = 0;

  String boundary = "----wardriverC5v2Boundary";
  String head1;
  head1.reserve(320);
  head1 += "--";
  head1 += boundary;
  head1 += "\r\nContent-Disposition: form-data; name=\"public_route\"\r\n\r\n";
  head1 += publicRoute ? "true" : "false";
  head1 += "\r\n--";
  head1 += boundary;
  head1 += "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"";
  head1 += filename;
  head1 += "\"\r\nContent-Type: text/csv\r\n\r\n";

  String tail;
  tail.reserve(64);
  tail += "\r\n--";
  tail += boundary;
  tail += "--\r\n";

  size_t total = head1.length() + fileSize + tail.length();

  WiFiClientSecure client;
  client.setCACert(WARDRIFT_CA_PEM);
  client.setTimeout(WD_CONNECT_TIMEOUT_MS);
  if (!client.connect(WD_HOST, WD_PORT)) {
    last_error_ = "route connect failed";
    return false;
  }

  String head;
  head.reserve(384);
  head += "POST ";
  head += WD_LOGS_PATH;
  head += " HTTP/1.1\r\nHost: ";
  head += WD_HOST;
  head += "\r\nUser-Agent: wardriver-c5v2/2.0\r\nAccept: application/json\r\nAuthorization: Bearer ";
  head += token;
  head += "\r\nConnection: close\r\nContent-Type: multipart/form-data; boundary=";
  head += boundary;
  head += "\r\nContent-Length: ";
  head += String((unsigned long)total);
  head += "\r\n\r\n";

  bool ok = writeAll(client, (const uint8_t*)head.c_str(), head.length(), WD_IO_TIMEOUT_MS) &&
            writeAll(client, (const uint8_t*)head1.c_str(), head1.length(), WD_IO_TIMEOUT_MS);

  uint8_t buf[2048];
  while (ok && file.available()) {
    size_t n = file.read(buf, sizeof(buf));
    if (n == 0) break;
    ok = writeAll(client, buf, (size_t)n, WD_IO_TIMEOUT_MS);
  }
  ok = ok && writeAll(client, (const uint8_t*)tail.c_str(), tail.length(), WD_IO_TIMEOUT_MS);
  if (!ok) {
    client.stop();
    last_error_ = "route short write";
    return false;
  }

  httpStatus = readResponse(client, response);
  client.stop();
  return true;
}

bool WardriftClient::testSignIn(const String& username, const String& password) {
  last_error_ = "";
  if (username.isEmpty() || password.isEmpty()) {
    last_error_ = "account credentials missing";
    return false;
  }
  if (!ensureClock()) return false;
  String token;
  if (!signIn(username, password, token)) return false;
  saveSessionToken(token);
  return true;
}

bool WardriftClient::uploadRoute(const String& filePath, const String& username,
                                 const String& password, bool publicRoute,
                                 Stats* stats) {
  last_error_ = "";
  Stats local;
  Stats& st = stats ? *stats : local;
  st = Stats();

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
  uint32_t fileSize = (uint32_t)f.size();
  if (fileSize == 0) {
    f.close();
    last_error_ = "empty log";
    return false;
  }

  String filename = leafName(filePath);
  filename.replace("\"", "_");
  filename.replace("\r", "_");
  filename.replace("\n", "_");

  String token;
  bool haveToken = loadSessionToken(token);

  display.clearScreen();
  display.drawCenteredText("Wardrift route", true);

  for (int attempt = 0; attempt < 3; attempt++) {
    if (!haveToken) {
      if (!signIn(username, password, token)) {
        f.close();
        return false;
      }
      saveSessionToken(token);
      haveToken = true;
    }

    if (!f.seek(0)) {
      f.close();
      last_error_ = "seek failed";
      return false;
    }

    int status = 0;
    String response;
    if (!sendRoute(token, filename, f, fileSize, publicRoute, response, status)) {
      f.close();
      return false;
    }

    if (status == 200 || status == 201 || status == 202) {
      String uploadId = jsonStringField(response, "upload_id");
      String uploadStatus = jsonStringField(response, "status");
      Logger::log(GUD_MSG, "[WARDRIFT] route upload_id=" + uploadId +
                  " status=" + (uploadStatus.isEmpty() ? "queued" : uploadStatus) +
                  " bytes=" + String((unsigned long)fileSize));
      st.batches++;
      f.close();
      return true;
    }

    if (status == 409 && response.indexOf("duplicate_route") >= 0) {
      Logger::log(GUD_MSG, "[WARDRIFT] route already uploaded (duplicate_route)");
      st.batches++;
      f.close();
      return true;
    }

    if (status == 401 || status == 403) {
      // Session expired or revoked. Drop it and sign in again once.
      wardriftClearSession();
      haveToken = false;
      Logger::log(WARN_MSG, "[WARDRIFT] Session rejected; signing in again");
      continue;
    }

    if (status == 0 || status == 429 || status >= 500) {
      delay(1500);
      continue;
    }

    last_error_ = "route HTTP " + String(status);
    String excerpt = response.substring(0, 240);
    excerpt.replace("\r", " ");
    excerpt.replace("\n", " ");
    if (!excerpt.isEmpty()) last_error_ += ": " + excerpt;
    Logger::log(WARN_MSG, "[WARDRIFT] " + last_error_);
    f.close();
    return false;
  }

  f.close();
  if (last_error_.isEmpty()) last_error_ = "route upload failed";
  Logger::log(WARN_MSG, "[WARDRIFT] " + last_error_);
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
  // End offset of each serialized item inside `items`, so a rejected batch can
  // be split without re-parsing the log.
  uint32_t itemEnd[WD_BATCH_ROWS];
  uint16_t batchRows = 0;
  bool ok = true;

  auto flush = [&]() -> bool {
    if (batchRows == 0) return true;
    String deviceTs = isoNow();

    // Split-on-rejection worklist. A 4xx that is not auth / sequence / rate
    // limit means the server refused the payload, usually one bad row. Halve
    // the range instead of failing the whole file; a single row that still
    // fails is dropped and logged. If nothing in the batch is accepted, the
    // rejection is treated as global and the upload fails.
    struct Range { uint16_t a, b; };
    Range stack[16];
    uint8_t sp = 0;
    stack[sp++] = {0, batchRows};
    uint16_t accepted = 0;
    uint16_t dropped = 0;
    bool hardFail = false;
    String failError;
    String lastErr;

    while (sp > 0 && !hardFail) {
      Range r = stack[--sp];
      String slice;
      const String* payload = &items;
      if (!(r.a == 0 && r.b == batchRows)) {
        uint32_t start = (r.a == 0) ? 0 : itemEnd[r.a - 1] + 1;
        uint32_t end = itemEnd[r.b - 1];
        slice = items.substring(start, end);
        payload = &slice;
      }

      String idem = batchIdem(apiKey, *payload);
      String err;
      int status = 0;
      bool sent = false;
      for (int attempt = 0; attempt < 3; attempt++) {
        uint64_t useq = seq + 1;
        if (useq > WD_SEQ_CEILING) useq = WD_SEQ_CEILING;
        if (sendBatch(apiKey, *payload, idem, useq, deviceTs, err, status)) {
          seq = useq;
          wardriftSaveSequence(seq);
          accepted += (uint16_t)(r.b - r.a);
          st.sent += (r.b - r.a);
          st.batches++;
          sent = true;
          break;
        }
        lastErr = err;
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
        if (status == 429) {
          delay(2000);
          continue;
        }
        if (status == 0 || status >= 500) {
          delay(400);
          continue;
        }
        break;  // auth / schema / other 4xx: retrying the same payload won't help
      }
      if (sent) continue;

      bool payloadReject = (status == 400 || status == 413 ||
                            status == 415 || status == 422);
      if (payloadReject && (r.b - r.a) > 1 && (uint8_t)(sp + 2) <= 16) {
        uint16_t mid = (uint16_t)(r.a + (r.b - r.a) / 2);
        stack[sp++] = {(uint16_t)mid, r.b};
        stack[sp++] = {r.a, (uint16_t)mid};
        Logger::log(WARN_MSG, "[WARDRIFT] batch rejected (" + err +
                    "); splitting rows " + String(r.a) + "-" + String(r.b));
        continue;
      }
      if (payloadReject && (r.b - r.a) == 1) {
        // One malformed row must not fail the whole file.
        String bssid = jsonStringField(slice, "bssid");
        st.skipped++;
        dropped++;
        Logger::log(WARN_MSG, "[WARDRIFT] dropped row bssid=" + bssid +
                    " (" + err + ")");
        // If nothing at all is accepted, the rejection is global (schema or
        // key scope), not one bad row. Stop instead of walking every item.
        if (accepted == 0 && dropped >= 10) {
          hardFail = true;
          failError = "payload rejected (" + err + ")";
          break;
        }
        continue;
      }

      hardFail = true;
      failError = err.isEmpty() ? "batch failed" : err;
    }

    if (hardFail || accepted == 0) {
      last_error_ = hardFail ? failError : (lastErr.isEmpty() ? "batch rejected" : lastErr);
      if (last_error_ == "sequence_replay")
        last_error_ = "sequence_replay: device key exhausted, mint a new key";
      Logger::log(WARN_MSG, "[WARDRIFT] Batch failed: " + last_error_);
      return false;
    }
    saveResume(filePath, totalSize, (uint32_t)f.position());
    items = "";
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
    itemEnd[batchRows] = items.length();
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
