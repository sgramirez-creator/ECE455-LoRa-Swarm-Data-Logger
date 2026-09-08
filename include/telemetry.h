#pragma once
#include <stdint.h>
#include <math.h>

//
// TelemetryRecord is the single measurement cycle's worth of data. It is the
// contract between the sensor layer (fills it), the datalogger (persists it) and
// the radio layer (serialises it for the mesh).
//
// The struct is packed and versioned so that a ground station can decode any
// firmware revision. Add fields at the END and bump TELEMETRY_SCHEMA_VERSION.
//

#define TELEMETRY_SCHEMA_VERSION  3

// Sentinel written into any field a sensor could not produce this cycle.
#define TELEMETRY_NAN             NAN

// Max nodes in a DS18B20 temperature profiling string.
#define TELEMETRY_TEMP_STRING_MAX 8

#pragma pack(push, 1)
struct TelemetryRecord {
    uint8_t  schema_version;   // == TELEMETRY_SCHEMA_VERSION
    uint16_t node_id;
    uint16_t seq;              // cycle counter, wraps — for packet-loss stats
    uint32_t uptime_s;         // seconds since boot
    uint32_t epoch_s;          // UTC from RTC/GNSS, 0 if unknown

    // Position (from GNSS)
    float    latitude_deg;
    float    longitude_deg;
    float    altitude_m;
    uint8_t  gnss_satellites;

    // Air / environment (BME680 class)
    float    air_temp_c;
    float    air_humidity_pct;
    float    air_pressure_hpa;
    float    gas_resistance_kohm;

    // Water quality (sonde over RS-485) — placeholders for Phase 2
    float    water_temp_c;
    float    water_ph;
    float    water_do_mgl;         // dissolved oxygen
    float    water_conductivity_uscm;
    float    water_turbidity_ntu;

    // Rain / weather station
    float    rain_mm;
    float    wind_speed_ms;
    float    wind_dir_deg;

    // Vertical temperature profile (DS18B20 string), shallow -> deep
    uint8_t  temp_string_count;
    float    temp_string_c[TELEMETRY_TEMP_STRING_MAX];

    // Housekeeping
    float    battery_v;
    float    solar_v;
    int8_t   last_rssi_dbm;
    uint8_t  flags;               // bitfield, see TELEMETRY_FLAG_*
};
#pragma pack(pop)

enum : uint8_t {
    TELEMETRY_FLAG_GNSS_FIX    = 1 << 0,
    TELEMETRY_FLAG_RTC_VALID   = 1 << 1,
    TELEMETRY_FLAG_LOW_BATTERY = 1 << 2,
    TELEMETRY_FLAG_SD_ERROR    = 1 << 3,
    TELEMETRY_FLAG_SENSOR_FAULT= 1 << 4,
};

// Initialise every measurement field to NAN and counters/flags to zero.
inline void telemetry_clear(TelemetryRecord& r) {
    r = TelemetryRecord{};
    r.schema_version = TELEMETRY_SCHEMA_VERSION;
    r.latitude_deg = r.longitude_deg = r.altitude_m = TELEMETRY_NAN;
    r.air_temp_c = r.air_humidity_pct = r.air_pressure_hpa = TELEMETRY_NAN;
    r.gas_resistance_kohm = TELEMETRY_NAN;
    r.water_temp_c = r.water_ph = r.water_do_mgl = TELEMETRY_NAN;
    r.water_conductivity_uscm = r.water_turbidity_ntu = TELEMETRY_NAN;
    r.rain_mm = r.wind_speed_ms = r.wind_dir_deg = TELEMETRY_NAN;
    r.battery_v = r.solar_v = TELEMETRY_NAN;
    r.temp_string_count = 0;
    for (int i = 0; i < TELEMETRY_TEMP_STRING_MAX; ++i)
        r.temp_string_c[i] = TELEMETRY_NAN;
}

// One CSV line (no trailing newline). buf should be >= 512 bytes.
int telemetry_to_csv(const TelemetryRecord& r, char* buf, int buflen);

// CSV header matching telemetry_to_csv(), for a fresh log file.
const char* telemetry_csv_header();

// Compact JSON with short keys; NaN fields are omitted. This is the mesh wire
// format (kept well under Meshtastic's ~230-byte text payload). buf >= 400.
// Returns the length written, or -1 if it would overflow buflen.
int telemetry_to_json(const TelemetryRecord& r, char* buf, int buflen);
