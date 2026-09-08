#include "sensors/BME680Sensor.h"
#include "config.h"
#include <Arduino.h>

bool BME680Sensor::begin() {
    Wire.begin();
    if (!dev_.begin(BME680_I2C_ADDR)) return false;
    dev_.setTemperatureOversampling(BME680_OS_8X);
    dev_.setHumidityOversampling(BME680_OS_2X);
    dev_.setPressureOversampling(BME680_OS_4X);
    dev_.setIIRFilterSize(BME680_FILTER_SIZE_3);
    dev_.setGasHeater(320, 150);   // 320 C for 150 ms
    return true;
}

void BME680Sensor::startMeasurement() {
    uint32_t ready = dev_.beginReading();          // async conversion
    warmup_ = ready ? (ready - millis() + 20) : 250;
}

bool BME680Sensor::read(TelemetryRecord& out) {
    if (!dev_.endReading()) return false;
    out.air_temp_c          = dev_.temperature;
    out.air_humidity_pct    = dev_.humidity;
    out.air_pressure_hpa    = dev_.pressure / 100.0f;
    out.gas_resistance_kohm = dev_.gas_resistance / 1000.0f;
    return true;
}
