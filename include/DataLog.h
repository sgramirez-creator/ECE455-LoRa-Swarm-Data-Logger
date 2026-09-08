#pragma once
#include "telemetry.h"

//
// Append-only telemetry log on the RP2040 internal flash (LittleFS).
//
// Every record that is measured is written here before any transmit attempt, so
// data survives a mesh outage or a failed uplink. Phase 3 can add a microSD
// backend behind the same API and an "unsent" marker for store-and-forward.
//
namespace datalog {

// Mount the filesystem and ensure the log file has a header. Returns false if
// the FS could not be mounted.
bool begin();

// Append one record as a CSV line. Returns false on write error.
bool append(const TelemetryRecord& r);

// Current log size in bytes (0 if not mounted).
uint32_t sizeBytes();

// Rotate the log (rename to .1 and start fresh) once it exceeds the cap.
void rotateIfNeeded();

}  // namespace datalog
