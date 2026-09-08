#include "telemetry.h"
#include <Arduino.h>

const char* telemetry_csv_header() {
    return "schema,node_id,seq,uptime_s,epoch_s,lat,lon,alt_m,sats,"
           "air_t_c,air_rh,air_p_hpa,gas_kohm,"
           "w_t_c,w_ph,w_do,w_cond,w_turb,"
           "rain_mm,wind_ms,wind_dir,"
           "ts_n,ts0,ts1,ts2,ts3,ts4,ts5,ts6,ts7,"
           "batt_v,solar_v,rssi,flags";
}

// Print a float or an empty cell when the value is NaN.
static int put_f(char* p, int n, float v, int decimals) {
    if (isnan(v)) return snprintf(p, n, ",");
    return snprintf(p, n, ",%.*f", decimals, (double)v);
}

int telemetry_to_csv(const TelemetryRecord& r, char* buf, int buflen) {
    int w = snprintf(buf, buflen, "%u,%u,%u,%lu,%lu",
                     r.schema_version, r.node_id, r.seq,
                     (unsigned long)r.uptime_s, (unsigned long)r.epoch_s);

    w += put_f(buf + w, buflen - w, r.latitude_deg, 6);
    w += put_f(buf + w, buflen - w, r.longitude_deg, 6);
    w += put_f(buf + w, buflen - w, r.altitude_m, 1);
    w += snprintf(buf + w, buflen - w, ",%u", r.gnss_satellites);

    w += put_f(buf + w, buflen - w, r.air_temp_c, 2);
    w += put_f(buf + w, buflen - w, r.air_humidity_pct, 1);
    w += put_f(buf + w, buflen - w, r.air_pressure_hpa, 2);
    w += put_f(buf + w, buflen - w, r.gas_resistance_kohm, 1);

    w += put_f(buf + w, buflen - w, r.water_temp_c, 2);
    w += put_f(buf + w, buflen - w, r.water_ph, 2);
    w += put_f(buf + w, buflen - w, r.water_do_mgl, 2);
    w += put_f(buf + w, buflen - w, r.water_conductivity_uscm, 1);
    w += put_f(buf + w, buflen - w, r.water_turbidity_ntu, 1);

    w += put_f(buf + w, buflen - w, r.rain_mm, 2);
    w += put_f(buf + w, buflen - w, r.wind_speed_ms, 2);
    w += put_f(buf + w, buflen - w, r.wind_dir_deg, 0);

    w += snprintf(buf + w, buflen - w, ",%u", r.temp_string_count);
    for (int i = 0; i < TELEMETRY_TEMP_STRING_MAX; ++i)
        w += put_f(buf + w, buflen - w, r.temp_string_c[i], 3);

    w += put_f(buf + w, buflen - w, r.battery_v, 3);
    w += put_f(buf + w, buflen - w, r.solar_v, 3);
    w += snprintf(buf + w, buflen - w, ",%d,%u", r.last_rssi_dbm, r.flags);
    return w;
}

// --- compact JSON for the mesh -------------------------------------------

// Append `"key":value` (comma-separated) only when v is finite.
static int j_f(char* p, int n, const char* key, float v, int dec) {
    if (isnan(v) || isinf(v)) return 0;
    return snprintf(p, n, ",\"%s\":%.*f", key, dec, (double)v);
}

int telemetry_to_json(const TelemetryRecord& r, char* buf, int buflen) {
    int w = snprintf(buf, buflen,
                     "{\"v\":%u,\"id\":%u,\"sq\":%u,\"up\":%lu,\"t\":%lu,\"fl\":%u",
                     r.schema_version, r.node_id, r.seq,
                     (unsigned long)r.uptime_s, (unsigned long)r.epoch_s, r.flags);

    w += j_f(buf + w, buflen - w, "lat", r.latitude_deg, 6);
    w += j_f(buf + w, buflen - w, "lon", r.longitude_deg, 6);
    w += j_f(buf + w, buflen - w, "alt", r.altitude_m, 1);
    if (r.gnss_satellites)
        w += snprintf(buf + w, buflen - w, ",\"sv\":%u", r.gnss_satellites);

    w += j_f(buf + w, buflen - w, "at", r.air_temp_c, 2);
    w += j_f(buf + w, buflen - w, "ah", r.air_humidity_pct, 1);
    w += j_f(buf + w, buflen - w, "ap", r.air_pressure_hpa, 1);
    w += j_f(buf + w, buflen - w, "gas", r.gas_resistance_kohm, 0);

    w += j_f(buf + w, buflen - w, "wt", r.water_temp_c, 2);
    w += j_f(buf + w, buflen - w, "ph", r.water_ph, 2);
    w += j_f(buf + w, buflen - w, "do", r.water_do_mgl, 2);
    w += j_f(buf + w, buflen - w, "ec", r.water_conductivity_uscm, 0);
    w += j_f(buf + w, buflen - w, "tb", r.water_turbidity_ntu, 1);

    w += j_f(buf + w, buflen - w, "rn", r.rain_mm, 2);
    w += j_f(buf + w, buflen - w, "ws", r.wind_speed_ms, 1);
    w += j_f(buf + w, buflen - w, "wd", r.wind_dir_deg, 0);

    if (r.temp_string_count) {
        w += snprintf(buf + w, buflen - w, ",\"ts\":[");
        for (uint8_t i = 0; i < r.temp_string_count; ++i) {
            if (i) w += snprintf(buf + w, buflen - w, ",");
            if (isnan(r.temp_string_c[i]))
                w += snprintf(buf + w, buflen - w, "null");
            else
                w += snprintf(buf + w, buflen - w, "%.2f", (double)r.temp_string_c[i]);
        }
        w += snprintf(buf + w, buflen - w, "]");
    }

    w += j_f(buf + w, buflen - w, "bv", r.battery_v, 3);
    w += j_f(buf + w, buflen - w, "pv", r.solar_v, 3);
    w += snprintf(buf + w, buflen - w, "}");

    return (w >= buflen) ? -1 : w;
}
