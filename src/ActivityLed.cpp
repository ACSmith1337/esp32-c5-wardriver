#include "ActivityLed.h"

static constexpr uint32_t CORE_LOST_GRACE_MS = 15000;

void ActivityLed::begin(uint8_t pin, uint8_t on_level, uint32_t now) {
  pin_      = pin;
  on_level_ = on_level;
  started_  = true;
  last_core_ms_ = now;
  pinMode(pin_, OUTPUT);
  digitalWrite(pin_, on_level_ == HIGH ? LOW : HIGH);  // off
  pattern_.boot(now);
  level_ = false;
}

void ActivityLed::tick(uint32_t now) {
  if (!started_) return;
  bool want = pattern_.level(now);
  if (want == level_) return;
  level_ = want;
  digitalWrite(pin_, want ? on_level_ : (on_level_ == HIGH ? LOW : HIGH));
}

void ActivityLed::boot(uint32_t now) {
  pattern_.boot(now);
}

void ActivityLed::scan(uint32_t now, uint16_t records) {
  pattern_.activity(now, records);
}

void ActivityLed::tx_fail(uint32_t now) {
  pattern_.tx_fail(now);
}

void ActivityLed::link(uint32_t now, bool core_present, bool require_core) {
  if (core_present) last_core_ms_ = now;
  bool lost = require_core && (now - last_core_ms_ > CORE_LOST_GRACE_MS);
  pattern_.set_fault(now, lost);
}
