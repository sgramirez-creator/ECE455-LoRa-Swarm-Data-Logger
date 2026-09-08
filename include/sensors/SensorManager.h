#pragma once
#include "sensors/ISensor.h"
#include "telemetry.h"

//
// Owns the list of sensors and drives a full measurement cycle against them.
// Sensors are registered at startup; the manager never takes ownership of the
// pointers (they are expected to be static/global objects).
//
class SensorManager {
public:
    static constexpr int MAX_SENSORS = 12;

    // Register a sensor. Call before begin(). Returns false if the table is full.
    bool add(ISensor* s);

    // begin() every registered sensor; marks each present/absent.
    // Returns the count that came up healthy.
    int begin();

    // Run one measurement cycle: powerUp -> startMeasurement -> warmup ->
    // read -> powerDown for every present sensor, filling `out`.
    // Sets TELEMETRY_FLAG_SENSOR_FAULT if any present sensor failed to read.
    void sample(TelemetryRecord& out);

    int count() const { return count_; }
    int presentCount() const;

private:
    ISensor* sensors_[MAX_SENSORS] = {nullptr};
    int count_ = 0;
};
