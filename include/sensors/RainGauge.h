#pragma once
#include "sensors/ISensor.h"

//
// Tipping-bucket rain gauge: a reed switch closes once per bucket tip. Counted
// by a pin-change interrupt with a software debounce. read() reports the rain
// accumulated since the previous cycle and resets the counter.
//
class RainGauge : public ISensor {
public:
    const char* name() const override { return "RainGauge"; }
    bool begin() override;
    bool read(TelemetryRecord& out) override;

    uint32_t tipsSinceReset() const;
};
