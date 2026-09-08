#pragma once
#include "telemetry.h"

//
// Plausibility checks applied to a completed TelemetryRecord before it is logged
// or transmitted. Out-of-range values are nulled (set to NaN) and
// TELEMETRY_FLAG_SENSOR_FAULT is raised. "Stuck" detection compares against the
// previous cycle. Bounds live in config.h.
//
namespace validate {

// Run all checks in place. Safe to call with VALIDATE_ENABLE == 0 (no-op).
void check(TelemetryRecord& r);

// Number of fields nulled on the most recent check() call.
int lastRejectCount();

}  // namespace validate
