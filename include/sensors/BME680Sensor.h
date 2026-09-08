#pragma once
#include "sensors/ISensor.h"
#include <Adafruit_BME680.h>

//
// RAK1906 / Bosch BME680 — air temperature, humidity, barometric pressure and
// VOC gas resistance. I2C on `Wire` (GP2/GP3 = WisBlock base sensor slots).
//
class BME680Sensor : public ISensor {
public:
    const char* name() const override { return "BME680"; }
    bool begin() override;
    void startMeasurement() override;
    uint32_t warmupMs() const override { return warmup_; }
    bool read(TelemetryRecord& out) override;

private:
    Adafruit_BME680 dev_{&Wire};
    uint32_t warmup_ = 200;
};
