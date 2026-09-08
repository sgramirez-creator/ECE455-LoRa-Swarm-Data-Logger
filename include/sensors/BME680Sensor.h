#pragma once
#include "sensors/ISensor.h"

//
// RAK1906 / BME680 environmental sensor (I2C on `Wire` = GP2/GP3).
//
// STUB: currently synthesises plausible values so the pipeline can be exercised
// without hardware. Phase 2: replace the body with Adafruit_BME680 or the Bosch
// BSEC library. Keep begin() returning false when the part does not ACK so the
// manager correctly marks it absent.
//
class BME680Sensor : public ISensor {
public:
    const char* name() const override { return "BME680"; }
    bool begin() override;
    uint32_t warmupMs() const override { return 250; }
    bool read(TelemetryRecord& out) override;

private:
    uint32_t reads_ = 0;
};
