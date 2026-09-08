#pragma once
#include <stdint.h>
#include "telemetry.h"

//
// Common interface for every environmental sensor on the node.
// Modelled on EnviroDIY ModularSensors: each driver knows how to power itself,
// warm up, take a reading, and write its results into the shared TelemetryRecord.
//
// A measurement cycle is: begin() once at boot, then per cycle
//   powerUp() -> startMeasurement() -> wait(warmupMs) -> read() -> powerDown()
//
class ISensor {
public:
    virtual ~ISensor() {}

    // Human-readable name for logs.
    virtual const char* name() const = 0;

    // Power on and probe the bus. Returns false if the sensor is absent/faulty;
    // the manager will then skip it for the rest of the session.
    virtual bool begin() = 0;

    // Optional: enable the sensor's power rail / exit sleep.
    virtual void powerUp() {}

    // Optional: kick off a conversion for sensors that need settling time.
    virtual void startMeasurement() {}

    // Milliseconds to wait after startMeasurement() before read() is valid.
    virtual uint32_t warmupMs() const { return 0; }

    // Take a reading and populate the relevant fields of `out`.
    // Returns false on a read error (leave fields as NaN).
    virtual bool read(TelemetryRecord& out) = 0;

    // Optional: cut power / enter low-power state between cycles.
    virtual void powerDown() {}

    bool present = false;   // set by the manager from begin()
};
