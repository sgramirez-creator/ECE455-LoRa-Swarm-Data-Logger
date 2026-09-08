#include "sensors/BME680Sensor.h"
#include <Arduino.h>

// TODO(phase2): probe I2C 0x76/0x77 on Wire and init the real driver.
bool BME680Sensor::begin() {
#if defined(BME680_STUB_ABSENT)
    return false;
#else
    return true;   // pretend the part is on the bus
#endif
}

bool BME680Sensor::read(TelemetryRecord& out) {
    // Synthetic diurnal-ish wander so downstream code sees changing values.
    float phase = (millis() / 60000.0f);
    out.air_temp_c        = 21.0f + 3.0f * sinf(phase);
    out.air_humidity_pct  = 55.0f + 10.0f * cosf(phase);
    out.air_pressure_hpa  = 1013.2f + 0.5f * sinf(phase / 3.0f);
    out.gas_resistance_kohm = 120.0f + 5.0f * (float)(reads_ % 7);
    reads_++;
    return true;
}
