#include "LedPattern.h"

void LedPattern::schedule(uint32_t now, uint8_t count, uint32_t on_ms, uint32_t off_ms) {
  if (count == 0) return;
  pulses_left   = (uint8_t)(count - 1);  // first pulse starts immediately
  pulse_on_ms   = on_ms;
  pulse_off_ms  = off_ms;
  active_until  = now + on_ms;
  next_pulse_at = now + on_ms + off_ms;
  last_activity = now;
  next_heartbeat = now + heartbeat_ms;
}

void LedPattern::boot(uint32_t now) {
  // Three quick flashes: "powered, firmware alive".
  schedule(now, 3, 60, 120);
  next_heartbeat = now + heartbeat_ms;
}

void LedPattern::activity(uint32_t now, uint16_t records) {
  last_activity  = now;
  next_heartbeat = now + heartbeat_ms;
  if (records == 0) return;

  // Encode scan yield in the flash count: 1 / 2 / 3 pulses.
  uint8_t count = 1;
  if (records >= 20)      count = 3;
  else if (records >= 5)  count = 2;
  schedule(now, count, 25, 90);
}

void LedPattern::tx_fail(uint32_t now) {
  // One long flash = a node->core send failed.
  schedule(now, 1, 200, 100);
}

void LedPattern::set_fault(uint32_t now, bool on) {
  if (on == fault) return;
  fault = on;
  fault_started = now;
  if (!on) next_heartbeat = now + heartbeat_ms;
}

bool LedPattern::level(uint32_t now) {
  if (fault) {
    // Two short flashes, then dark until the next period.
    uint32_t phase = (now - fault_started) % fault_period;
    if (phase < fault_on) return true;
    if (phase >= fault_gap && phase < fault_gap + fault_on) return true;
    return false;
  }

  if (now < active_until) return true;

  if (pulses_left > 0 && now >= next_pulse_at) {
    pulses_left--;
    active_until  = now + pulse_on_ms;
    next_pulse_at = now + pulse_on_ms + pulse_off_ms;
    return true;
  }

  if (now >= next_heartbeat) {
    next_heartbeat = now + heartbeat_ms;
    active_until   = now + heartbeat_on;
    return true;
  }

  return false;
}
