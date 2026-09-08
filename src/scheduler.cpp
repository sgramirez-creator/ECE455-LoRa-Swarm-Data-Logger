#include "scheduler.h"
#include "config.h"
#include <Arduino.h>

static constexpr uint32_t EPOCH_FLOOR = 1672531200UL;  // 2023-01-01

void Scheduler::begin(bool runAtStart) {
    uint32_t now = millis();
    nextAtMs_ = runAtStart ? now : now + intervalMs_;
    armed_    = true;
}

bool Scheduler::due(uint32_t epochNow) {
    if (!armed_) return false;

#if SCHEDULE_EPOCH_ALIGNED
    if (epochNow >= EPOCH_FLOOR && intervalS_) {
        uint32_t bucket = epochNow / intervalS_;
        if (firstEpoch_) {                 // adopt current bucket without firing
            lastBucket_ = bucket;
            firstEpoch_ = false;
            return false;
        }
        if (bucket != lastBucket_) {
            lastBucket_ = bucket;
            cycles_++;
            return true;
        }
        return false;
    }
#endif

    // free-running fallback
    if ((int32_t)(millis() - nextAtMs_) < 0) return false;
    nextAtMs_ += intervalMs_;
    if ((int32_t)(millis() - nextAtMs_) > 0) nextAtMs_ = millis() + intervalMs_;
    cycles_++;
    return true;
}

uint32_t Scheduler::msUntilNext(uint32_t epochNow) const {
#if SCHEDULE_EPOCH_ALIGNED
    if (epochNow >= EPOCH_FLOOR && intervalS_) {
        uint32_t into = epochNow % intervalS_;
        return (intervalS_ - into) * 1000UL;
    }
#endif
    if (!armed_) return intervalMs_;
    int32_t d = (int32_t)(nextAtMs_ - millis());
    return d < 0 ? 0 : (uint32_t)d;
}
