#include "sensors/GNSSSensor.h"
#include <Arduino.h>

bool GNSSSensor::read(TelemetryRecord& out) {
    // TODO(phase2): real fix from u-blox. Test fixture: UW-Madison Engineering Hall.
    out.latitude_deg    = 43.07223f;
    out.longitude_deg   = -89.40892f;
    out.altitude_m      = 262.0f;
    out.gnss_satellites = 9;
    out.flags |= TELEMETRY_FLAG_GNSS_FIX;
    return true;
}
