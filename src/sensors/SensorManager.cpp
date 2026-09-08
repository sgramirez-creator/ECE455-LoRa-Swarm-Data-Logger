#include "sensors/SensorManager.h"
#include <Arduino.h>

bool SensorManager::add(ISensor* s) {
    if (count_ >= MAX_SENSORS || s == nullptr) return false;
    sensors_[count_++] = s;
    return true;
}

int SensorManager::begin() {
    int ok = 0;
    for (int i = 0; i < count_; ++i) {
        ISensor* s = sensors_[i];
        s->present = s->begin();
        Serial.printf("  [sensor] %-16s %s\n", s->name(),
                      s->present ? "OK" : "absent");
        if (s->present) ok++;
    }
    return ok;
}

int SensorManager::presentCount() const {
    int n = 0;
    for (int i = 0; i < count_; ++i)
        if (sensors_[i]->present) n++;
    return n;
}

void SensorManager::sample(TelemetryRecord& out) {
    // Power up + kick off conversions for all sensors first, so their warmup
    // windows overlap instead of running back-to-back.
    uint32_t maxWarmup = 0;
    for (int i = 0; i < count_; ++i) {
        ISensor* s = sensors_[i];
        if (!s->present) continue;
        s->powerUp();
        s->startMeasurement();
        maxWarmup = max(maxWarmup, s->warmupMs());
    }

    if (maxWarmup) delay(maxWarmup);

    for (int i = 0; i < count_; ++i) {
        ISensor* s = sensors_[i];
        if (!s->present) continue;
        if (!s->read(out)) {
            out.flags |= TELEMETRY_FLAG_SENSOR_FAULT;
            Serial.printf("  [sensor] %s read failed\n", s->name());
        }
        s->powerDown();
    }
}
