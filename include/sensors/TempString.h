#pragma once
#include "sensors/ISensor.h"
#include "config.h"
#include <OneWire.h>
#include <DallasTemperature.h>

//
// Vertical temperature profiling string: a chain of DS18B20 sensors on a single
// 1-Wire bus. Reported shallow -> deep in bus-discovery order (see
// docs/03-phase2-sensors.md for pinning them to physical depth).
//
class TempString : public ISensor {
public:
    const char* name() const override { return "TempString"; }
    bool begin() override;
    void startMeasurement() override;
    uint32_t warmupMs() const override { return 800; }   // 12-bit conversion
    bool read(TelemetryRecord& out) override;

    uint8_t deviceCount() const { return count_; }

private:
    OneWire           wire_{TEMP_STRING_ONEWIRE_PIN};
    DallasTemperature bus_{&wire_};
    uint8_t           count_ = 0;
    DeviceAddress     addr_[TELEMETRY_TEMP_STRING_MAX];
};
