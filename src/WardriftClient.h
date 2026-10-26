#pragma once
#include <Arduino.h>
#include <FS.h>

// wardrift.net client.
//
// Two upload paths:
//   * Account route: sign in at /v1/auth/sign-in/username, then POST the raw
//     WiGLE CSV to /v1/wardrive/logs as multipart. This creates an archived
//     route with distance, AP count and streak, exactly like the web upload.
//   * Device key: POST /v1/ingest/signals with X-API-Key. Individual signal
//     observations; it does not create an archived route.
// TLS is pinned to the ISRG roots in WardriftTrust.h; never setInsecure().
class WardriftClient {
 public:
  struct Stats {
    uint32_t sent    = 0;
    uint32_t skipped = 0;
    uint32_t batches = 0;
  };

  // Upload every ingestable row in a stock WiGLE-format log on the SD card.
  // Caller is responsible for having joined WiFi. Returns true only when
  // every batch was accepted (or confirmed duplicate).
  bool uploadFile(const String& filePath, const String& apiKey, Stats* stats = nullptr);

  // Upload the raw log as one archived route through the signed-in account
  // path. Signs in when no cached session exists, and again once on a 401.
  bool uploadRoute(const String& filePath, const String& username,
                   const String& password, bool publicRoute,
                   Stats* stats = nullptr);

  // Sign in with the supplied credentials and cache the session token.
  // Used by the config page's Test sign-in action; uploads nothing.
  bool testSignIn(const String& username, const String& password);

  const String& lastError() const { return last_error_; }

 private:
  bool ensureClock();
  bool signIn(const String& username, const String& password, String& token);
  bool sendRoute(const String& token, const String& filename, File& file,
                 uint32_t fileSize, bool publicRoute, String& response,
                 int& httpStatus);
  bool sendBatch(const String& apiKey, const String& items, const String& idem,
                 uint64_t seq, const String& deviceTs, String& error, int& httpStatus);
  String last_error_;
};

uint64_t wardriftLoadSequence();
void     wardriftSaveSequence(uint64_t seq);
bool     wardriftSessionCached();
void     wardriftClearSession();
