#pragma once
#include <Arduino.h>
#include "LedPattern.h"

// Thin Arduino binding around LedPattern. Owns pin polarity and the
// "is the node linked to a core" grace timer. Non-blocking: tick() is
// called from loop().
class ActivityLed {
 public:
  void begin(uint8_t pin, uint8_t on_level, uint32_t now);
  void tick(uint32_t now);
  void boot(uint32_t now);
  void scan(uint32_t now, uint16_t records);
  void tx_fail(uint32_t now);
  // require_core: node is bound/encrypted and needs a core link.
  void link(uint32_t now, bool core_present, bool require_core);

 private:
  LedPattern pattern_;
  uint8_t pin_      = 255;
  uint8_t on_level_ = HIGH;
  bool    level_    = false;
  bool    started_  = false;
  uint32_t last_core_ms_ = 0;
};
