#pragma once
#include <stdint.h>

//
// Measurement scheduler with two modes:
//
//  * epoch-aligned  — fires on wall-clock boundaries (:00, :30, ...) so every
//                     node in the swarm samples at the same instants. Needs a
//                     valid UTC epoch passed into due()/msUntilNext().
//  * free-running    — fixed millis() interval. Used until the RTC has time,
//                     or when SCHEDULE_EPOCH_ALIGNED is 0.
//
// due() latches one cycle per boundary; poll it from loop().
//
class Scheduler {
public:
    explicit Scheduler(uint32_t intervalMs)
        : intervalMs_(intervalMs), intervalS_(intervalMs / 1000UL) {}

    void begin(bool runAtStart = true);

    // epochNow: current UTC seconds, or 0 if unknown (forces free-running mode).
    bool     due(uint32_t epochNow = 0);
    uint32_t msUntilNext(uint32_t epochNow = 0) const;

    uint32_t cycleCount() const { return cycles_; }

private:
    uint32_t intervalMs_;
    uint32_t intervalS_;
    uint32_t nextAtMs_   = 0;    // free-running mode
    uint32_t lastBucket_ = 0;    // epoch-aligned mode: epochNow / intervalS_
    uint32_t cycles_     = 0;
    bool     armed_      = false;
    bool     firstEpoch_ = true;
};
