#include "sensors/TempString.h"
#include <Arduino.h>
#include <math.h>

bool TempString::begin() {
    bus_.begin();
    bus_.setResolution(TEMP_STRING_RESOLUTION_BITS);
    bus_.setWaitForConversion(false);   // we manage the warmup ourselves

    uint8_t n = bus_.getDeviceCount();
    count_ = n > TELEMETRY_TEMP_STRING_MAX ? TELEMETRY_TEMP_STRING_MAX : n;
    for (uint8_t i = 0; i < count_; ++i) bus_.getAddress(addr_[i], i);

    Serial.printf("  [tempstring] %u DS18B20 found\n", count_);
    return count_ > 0;
}

void TempString::startMeasurement() {
    bus_.requestTemperatures();
}

bool TempString::read(TelemetryRecord& out) {
    bool any = false;
    for (uint8_t i = 0; i < count_; ++i) {
        float c = bus_.getTempC(addr_[i]);
        if (c == DEVICE_DISCONNECTED_C || c <= -100.0f) {
            out.temp_string_c[i] = TELEMETRY_NAN;
            continue;
        }
        out.temp_string_c[i] = c;
        any = true;
    }
    out.temp_string_count = count_;

    // Give single-value consumers the shallowest reading if nothing else set it.
    if (isnan(out.water_temp_c) && count_ > 0 && !isnan(out.temp_string_c[0]))
        out.water_temp_c = out.temp_string_c[0];

    return any;
}
