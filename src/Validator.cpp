#include "Validator.h"
#include "config.h"
#include <Arduino.h>
#include <math.h>

namespace validate {

static int s_rejects = 0;

#if VALIDATE_ENABLE
// Range gate: null the field and flag a fault if it is present but out of range.
static bool gate(float& v, float lo, float hi, TelemetryRecord& r, const char* tag) {
    if (isnan(v)) return true;              // absent, not a fault
    if (v < lo || v > hi) {
        Serial.printf("  [validate] %s = %.3f out of [%.1f, %.1f] -> nulled\n",
                      tag, (double)v, (double)lo, (double)hi);
        v = TELEMETRY_NAN;
        r.flags |= TELEMETRY_FLAG_SENSOR_FAULT;
        s_rejects++;
        return false;
    }
    return true;
}

// Stuck detection: bit-identical value on a sensor for N consecutive cycles.
struct StuckState { float last; uint8_t run; };
static StuckState s_stuck[6] = {};

static void stuck(float v, int idx, TelemetryRecord& r, const char* tag) {
    if (isnan(v)) { s_stuck[idx].run = 0; return; }
    if (v == s_stuck[idx].last) {
        if (++s_stuck[idx].run >= VALIDATE_STUCK_CYCLES) {
            Serial.printf("  [validate] %s stuck at %.3f for %u cycles\n",
                          tag, (double)v, s_stuck[idx].run);
            r.flags |= TELEMETRY_FLAG_SENSOR_FAULT;
        }
    } else {
        s_stuck[idx].last = v;
        s_stuck[idx].run = 1;
    }
}
#endif  // VALIDATE_ENABLE

int lastRejectCount() { return s_rejects; }

void check(TelemetryRecord& r) {
#if VALIDATE_ENABLE
    s_rejects = 0;

    gate(r.air_temp_c,       VALIDATE_AIR_T_MIN,  VALIDATE_AIR_T_MAX,  r, "air_t");
    gate(r.air_humidity_pct, VALIDATE_AIR_RH_MIN, VALIDATE_AIR_RH_MAX, r, "air_rh");
    gate(r.air_pressure_hpa, VALIDATE_AIR_P_MIN,  VALIDATE_AIR_P_MAX,  r, "air_p");

    gate(r.water_temp_c,            VALIDATE_WATER_T_MIN, VALIDATE_WATER_T_MAX, r, "water_t");
    gate(r.water_ph,               VALIDATE_PH_MIN,   VALIDATE_PH_MAX,   r, "ph");
    gate(r.water_do_mgl,           VALIDATE_DO_MIN,   VALIDATE_DO_MAX,   r, "do");
    gate(r.water_conductivity_uscm,VALIDATE_COND_MIN, VALIDATE_COND_MAX, r, "cond");
    gate(r.water_turbidity_ntu,    VALIDATE_TURB_MIN, VALIDATE_TURB_MAX, r, "turb");

    gate(r.wind_speed_ms, VALIDATE_WIND_MS_MIN, VALIDATE_WIND_MS_MAX, r, "wind");
    if (!isnan(r.wind_dir_deg) && (r.wind_dir_deg < 0.0f || r.wind_dir_deg >= 360.0f))
        r.wind_dir_deg = fmodf(fmodf(r.wind_dir_deg, 360.0f) + 360.0f, 360.0f);
    gate(r.rain_mm, 0.0f, VALIDATE_RAIN_MM_MAX, r, "rain");

    gate(r.latitude_deg,  -VALIDATE_LAT_ABS_MAX, VALIDATE_LAT_ABS_MAX, r, "lat");
    gate(r.longitude_deg, -VALIDATE_LON_ABS_MAX, VALIDATE_LON_ABS_MAX, r, "lon");
    // A (0,0) fix is the classic GNSS "no real fix" artefact.
    if (r.latitude_deg == 0.0f && r.longitude_deg == 0.0f) {
        r.latitude_deg = r.longitude_deg = TELEMETRY_NAN;
        r.flags &= ~TELEMETRY_FLAG_GNSS_FIX;
    }

    for (uint8_t i = 0; i < r.temp_string_count && i < TELEMETRY_TEMP_STRING_MAX; ++i)
        gate(r.temp_string_c[i], VALIDATE_WATER_T_MIN, VALIDATE_WATER_T_MAX, r, "ts");

    stuck(r.air_temp_c,    0, r, "air_t");
    stuck(r.water_temp_c,  1, r, "water_t");
    stuck(r.water_ph,      2, r, "ph");
    stuck(r.water_do_mgl,  3, r, "do");
    stuck(r.air_pressure_hpa, 4, r, "air_p");
    stuck(r.battery_v,     5, r, "batt");

    if (s_rejects)
        Serial.printf("  [validate] %d field(s) rejected\n", s_rejects);
#else
    (void)r;
#endif
}

}  // namespace validate
