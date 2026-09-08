#pragma once
#include "sensors/ISensor.h"
#include <RV-3028-C7.h>

//
// RAK12002 / Micro Crystal RV-3028-C7 — battery-backed real-time clock.
// Provides the wall-clock epoch for every record and, in Phase 3, the alarm
// that wakes the MCU from deep sleep on the 30-minute boundary.
//
class RtcSensor : public ISensor {
public:
    const char* name() const override { return "RTC"; }
    bool begin() override;
    bool read(TelemetryRecord& out) override;

    // Push an authoritative epoch (e.g. from GNSS) into the RTC if it has
    // drifted more than `toleranceS` seconds. No-op if the RTC is absent.
    void syncTo(uint32_t epoch, uint32_t toleranceS = 2);

    uint32_t epoch();
    bool present() const { return ok_; }

    // Program the RV-3028 periodic countdown timer to pull INT low every
    // `seconds`, auto-reloading. This is the deep-sleep wake source.
    void armPeriodicTimer(uint32_t seconds);

    // Acknowledge the timer interrupt (call after each wake).
    void clearTimerFlag();

private:
    RV3028 dev_;
    bool   ok_ = false;
};
