#pragma once
#include "sensors/ISensor.h"
#include <SparkFun_u-blox_GNSS_Arduino_Library.h>

//
// RAK12500 / u-blox ZOE-M8Q GNSS over I2C (address 0x42).
//
// Fills position + satellite count and, when the fix carries valid time, the
// UTC epoch. main() uses lastEpoch() to discipline the RTC.
//
class GNSSSensor : public ISensor {
public:
    const char* name() const override { return "GNSS"; }
    bool begin() override;
    void powerUp() override;
    bool read(TelemetryRecord& out) override;
    void powerDown() override;

    // UTC epoch from the last fix with valid time, or 0.
    uint32_t lastEpoch() const { return lastEpoch_; }

private:
    SFE_UBLOX_GNSS dev_;
    uint32_t lastEpoch_ = 0;
};
