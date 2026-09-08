#include "sensors/RtcSensor.h"
#include "config.h"
#include <Arduino.h>

// Sanity floor: 2023-01-01. Anything below this is an unset / invalid clock.
static constexpr uint32_t EPOCH_FLOOR = 1672531200UL;

bool RtcSensor::begin() {
    Wire.begin();
    ok_ = dev_.begin(Wire);
    return ok_;
}

uint32_t RtcSensor::epoch() {
    if (!ok_) return 0;
    dev_.updateTime();
    return dev_.getUNIX();
}

bool RtcSensor::read(TelemetryRecord& out) {
    if (!ok_) return false;
    uint32_t e = epoch();
    if (e >= EPOCH_FLOOR) {
        out.epoch_s = e;
        out.flags |= TELEMETRY_FLAG_RTC_VALID;
    }
    return true;
}

void RtcSensor::syncTo(uint32_t epoch, uint32_t toleranceS) {
    if (!ok_ || epoch < EPOCH_FLOOR) return;
    dev_.updateTime();
    uint32_t cur = dev_.getUNIX();
    uint32_t drift = (cur > epoch) ? (cur - epoch) : (epoch - cur);
    if (drift > toleranceS) {
        dev_.setUNIX(epoch);
        Serial.printf("  [rtc] disciplined from GNSS, drift was %lu s\n",
                      (unsigned long)drift);
    }
}

void RtcSensor::armPeriodicTimer(uint32_t seconds) {
    if (!ok_ || seconds == 0) return;
    // RV-3028 countdown value is 12-bit. Use the 1 Hz clock for <= 4095 s,
    // otherwise the 1/60 Hz clock and a value in minutes.
    if (seconds <= 4095) {
        dev_.setTimer(/*repeat=*/true, /*frequency=*/1, (uint16_t)seconds,
                      /*setInterrupt=*/true, /*start=*/true);
    } else {
        uint16_t minutes = (uint16_t)((seconds + 59) / 60);
        if (minutes > 4095) minutes = 4095;
        dev_.setTimer(true, 60000, minutes, true, true);
    }
    Serial.printf("  [rtc] periodic wake timer armed: %lu s\n",
                  (unsigned long)seconds);
}

void RtcSensor::clearTimerFlag() {
    if (ok_) dev_.clearTimerInterruptFlag();
}
