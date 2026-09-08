#pragma once
#include "sensors/ISensor.h"

//
// RAK12500 / u-blox ZOE-M8Q GNSS (I2C or UART).
//
// STUB: reports a fixed test coordinate. Phase 2: use SparkFun_u-blox_GNSS_v3,
// power the module only long enough to get a fix, and set the RTC + epoch_s from
// GNSS time. Returns TELEMETRY_FLAG_GNSS_FIX when the fix is valid.
//
class GNSSSensor : public ISensor {
public:
    const char* name() const override { return "GNSS"; }
    bool begin() override { return true; }
    // A cold GNSS fix can take minutes; keep this realistic once wired.
    uint32_t warmupMs() const override { return 0; }
    bool read(TelemetryRecord& out) override;
};
