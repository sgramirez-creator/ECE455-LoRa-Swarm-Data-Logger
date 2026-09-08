#include "sensors/ModbusSonde.h"
#include "config.h"
#include "Rs485Bus.h"
#include <Arduino.h>

static bool readParam(modbusMaster& mb, int reg, float& dst) {
    if (reg < 0) return false;
    float v = mb.float32FromRegister((byte)SONDE_REG_FUNCTION, reg, bigEndian);
    if (isnan(v) || v <= -9990.0f) return false;   // library error sentinel
    dst = v;
    return true;
}

bool ModbusSonde::begin() {
    rs485::begin();
    mb_.begin(SONDE_MODBUS_ID, rs485::stream(), rs485::enablePin());

    // Probe: read one register at the first configured offset.
    int probe = SONDE_REG_TEMP_C >= 0 ? SONDE_REG_TEMP_C : SONDE_REG_PH;
    ok_ = probe >= 0 && mb_.getRegisters((byte)SONDE_REG_FUNCTION, probe, 2) != 0;
    return ok_;
}

bool ModbusSonde::read(TelemetryRecord& out) {
    if (!ok_) return false;
    bool any = false;
    any |= readParam(mb_, SONDE_REG_TEMP_C,    out.water_temp_c);
    any |= readParam(mb_, SONDE_REG_PH,        out.water_ph);
    any |= readParam(mb_, SONDE_REG_DO_MGL,    out.water_do_mgl);
    any |= readParam(mb_, SONDE_REG_COND_USCM, out.water_conductivity_uscm);
    any |= readParam(mb_, SONDE_REG_TURB_NTU,  out.water_turbidity_ntu);
    return any;
}
