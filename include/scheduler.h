#pragma once
#include <stdint.h>

//
// Fixed-interval measurement scheduler.
//
// Phase 1: free-running on millis(). Phase 3: swap the time source for an
// external RTC alarm so the MCU can be in deep sleep between cycles and wake on
// the alarm edge. The public API is deliberately time-source agnostic.
//
class Scheduler {
public:
    explicit Scheduler(uint32_t intervalMs) : intervalMs_(intervalMs) {}

    // Call once after boot. Optionally fire the first cycle immediately.
    void begin(bool runAtStart = true);

    // Returns true exactly once per interval. Poll it from loop().
    bool due();

    // Milliseconds until the next cycle (for choosing a sleep duration).
    uint32_t msUntilNext() const;

    uint32_t cycleCount() const { return cycles_; }

private:
    uint32_t intervalMs_;
    uint32_t nextAt_ = 0;
    uint32_t cycles_ = 0;
    bool     armed_  = false;
};
