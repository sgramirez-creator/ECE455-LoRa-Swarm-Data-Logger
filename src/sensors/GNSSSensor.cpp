#include "sensors/GNSSSensor.h"
#include "config.h"
#include <Arduino.h>

bool GNSSSensor::begin() {
    Wire.begin();
    if (!dev_.begin(Wire, UBLOX_I2C_ADDR)) return false;
    dev_.setI2COutput(COM_TYPE_UBX);
    dev_.setNavigationFrequency(1);
    dev_.saveConfigSelective(VAL_CFG_SUBSEC_IOPORT);
    return true;
}

void GNSSSensor::powerUp() {
    dev_.powerSaveMode(false);
}

bool GNSSSensor::read(TelemetryRecord& out) {
    if (!dev_.getPVT(1200)) return false;   // wait up to 1.2 s for a fresh solution

    uint8_t fix = dev_.getFixType();        // 0 none, 2 2D, 3 3D
    out.gnss_satellites = dev_.getSIV();

    if (fix >= 2) {
        out.latitude_deg  = dev_.getLatitude()  * 1e-7;
        out.longitude_deg = dev_.getLongitude() * 1e-7;
        out.altitude_m    = dev_.getAltitudeMSL() / 1000.0f;
        out.flags |= TELEMETRY_FLAG_GNSS_FIX;
    }

    if (dev_.getTimeValid() && dev_.getDateValid()) {
        lastEpoch_   = dev_.getUnixEpoch();
        out.epoch_s  = lastEpoch_;
    }
    return true;
}

void GNSSSensor::powerDown() {
    dev_.powerSaveMode(true);
}
