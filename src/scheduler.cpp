#include "scheduler.h"
#include <Arduino.h>

void Scheduler::begin(bool runAtStart) {
    uint32_t now = millis();
    nextAt_ = runAtStart ? now : now + intervalMs_;
    armed_  = true;
}

bool Scheduler::due() {
    if (!armed_) return false;
    // Unsigned subtraction handles millis() rollover correctly.
    if ((int32_t)(millis() - nextAt_) < 0) return false;
    nextAt_ += intervalMs_;
    // If we fell far behind (e.g. long blocking read), don't burst-fire.
    if ((int32_t)(millis() - nextAt_) > 0) nextAt_ = millis() + intervalMs_;
    cycles_++;
    return true;
}

uint32_t Scheduler::msUntilNext() const {
    if (!armed_) return intervalMs_;
    int32_t d = (int32_t)(nextAt_ - millis());
    return d < 0 ? 0 : (uint32_t)d;
}
