#include "sensors/WeatherStation.h"
#include "config.h"
#include "Rs485Bus.h"
#include <Arduino.h>

static bool readParam(modbusMaster& mb, int reg, float& dst) {
    if (reg < 0) return false;
    float v = mb.float32FromRegister((byte)WX_REG_FUNCTION, reg, bigEndian);
    if (isnan(v) || v <= -9990.0f) return false;
    dst = v;
    return true;
}

bool WeatherStation::begin() {
    rs485::begin();
    mb_.begin(WX_MODBUS_ID, rs485::stream(), rs485::enablePin());

    int probe = WX_REG_WIND_SPEED_MS >= 0 ? WX_REG_WIND_SPEED_MS : WX_REG_WIND_DIR_DEG;
    ok_ = probe >= 0 && mb_.getRegisters((byte)WX_REG_FUNCTION, probe, 2) != 0;
    return ok_;
}

bool WeatherStation::read(TelemetryRecord& out) {
    if (!ok_) return false;
    bool any = false;
    any |= readParam(mb_, WX_REG_WIND_SPEED_MS, out.wind_speed_ms);
    any |= readParam(mb_, WX_REG_WIND_DIR_DEG,  out.wind_dir_deg);
    any |= readParam(mb_, WX_REG_AIR_TEMP_C,    out.air_temp_c);
    any |= readParam(mb_, WX_REG_AIR_RH_PCT,    out.air_humidity_pct);
    any |= readParam(mb_, WX_REG_AIR_PRESS_HPA, out.air_pressure_hpa);
    return any;
}
