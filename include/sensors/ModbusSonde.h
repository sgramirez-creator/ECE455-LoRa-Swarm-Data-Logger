#pragma once
#include "sensors/ISensor.h"
#include <SensorModbusMaster.h>

//
// Multi-parameter water-quality sonde on the RS485 bus, read over Modbus RTU.
//
// The register map (slave id, function code, per-parameter register offsets) is
// in config.h — set an offset to -1 to skip that parameter. Values are read as
// big-endian float32 pairs; flip to littleEndian in the .cpp if your sonde
// word-swaps.  Verified against: <fill in sonde model + manual section>.
//
class ModbusSonde : public ISensor {
public:
    const char* name() const override { return "Sonde"; }
    bool begin() override;
    uint32_t warmupMs() const override { return 1000; }
    bool read(TelemetryRecord& out) override;

private:
    modbusMaster mb_;
    bool ok_ = false;
};
