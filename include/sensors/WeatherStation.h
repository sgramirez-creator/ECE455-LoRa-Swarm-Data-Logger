#pragma once
#include "sensors/ISensor.h"
#include <SensorModbusMaster.h>

//
// Integrated weather station on the RS485 bus (Modbus RTU): wind speed and
// direction, optionally air temperature / humidity / pressure when the station
// is the authoritative met source (otherwise leave those offsets at -1 and let
// the BME680 provide them). Register map in config.h.
//
class WeatherStation : public ISensor {
public:
    const char* name() const override { return "Weather"; }
    bool begin() override;
    uint32_t warmupMs() const override { return 500; }
    bool read(TelemetryRecord& out) override;

private:
    modbusMaster mb_;
    bool ok_ = false;
};
