#pragma once
// Pure, dependency-free LED pattern engine.
// No Arduino types: the same code is compiled into firmware and into the
// host-side unit tests, so timing behavior is proven off-device.

#include <stdint.h>

struct LedPattern {
  // Pulse timing (milliseconds).
  uint32_t pulse_on_ms   = 25;
  uint32_t pulse_off_ms  = 90;
  uint32_t heartbeat_ms  = 2500;
  uint32_t heartbeat_on  = 12;
  uint32_t fault_period  = 2000;
  uint32_t fault_on      = 60;
  uint32_t fault_gap     = 120;

  // Scheduled pulse train.
  uint32_t active_until  = 0;
  uint32_t next_pulse_at = 0;
  uint8_t  pulses_left   = 0;

  // Heartbeat.
  uint32_t last_activity = 0;
  uint32_t next_heartbeat = 0;

  // Fault beacon (two quick flashes every fault_period).
  bool     fault         = false;
  uint32_t fault_started = 0;

  void boot(uint32_t now);
  void activity(uint32_t now, uint16_t records);
  void tx_fail(uint32_t now);
  void set_fault(uint32_t now, bool on);
  bool level(uint32_t now);

 private:
  void schedule(uint32_t now, uint8_t count, uint32_t on_ms, uint32_t off_ms);
};
