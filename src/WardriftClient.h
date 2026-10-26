#pragma once
#include <Arduino.h>

// wardrift.net device ingest client.
//
// Device-key path only: POST https://wardrift.net/v1/ingest/signals with
// X-API-Key. The account/multipart path is not usable by a device key.
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

  const String& lastError() const { return last_error_; }

 private:
  bool ensureClock();
  bool sendBatch(const String& apiKey, const String& items, const String& idem,
                 uint64_t seq, const String& deviceTs, String& error, int& httpStatus);
  String last_error_;
};

uint64_t wardriftLoadSequence();
void     wardriftSaveSequence(uint64_t seq);
