#include "DataLog.h"
#include "config.h"
#include <Arduino.h>
#include <LittleFS.h>

namespace datalog {

static bool mounted = false;

bool begin() {
    mounted = LittleFS.begin();
    if (!mounted) {
        Serial.println(F("  [datalog] LittleFS mount failed"));
        return false;
    }
    if (!LittleFS.exists(DATALOG_PATH)) {
        File f = LittleFS.open(DATALOG_PATH, "w");
        if (!f) { mounted = false; return false; }
        f.println(telemetry_csv_header());
        f.close();
    }
    Serial.printf("  [datalog] ready, %lu bytes on disk\n",
                  (unsigned long)sizeBytes());
    return true;
}

bool append(const TelemetryRecord& r) {
    if (!mounted) return false;
    char line[512];
    telemetry_to_csv(r, line, sizeof(line));

    File f = LittleFS.open(DATALOG_PATH, "a");
    if (!f) return false;
    f.println(line);
    f.close();
    return true;
}

uint32_t sizeBytes() {
    if (!mounted) return 0;
    File f = LittleFS.open(DATALOG_PATH, "r");
    if (!f) return 0;
    uint32_t n = f.size();
    f.close();
    return n;
}

void rotateIfNeeded() {
    if (!mounted || sizeBytes() < DATALOG_MAX_BYTES) return;
    LittleFS.remove(DATALOG_PATH ".1");
    LittleFS.rename(DATALOG_PATH, DATALOG_PATH ".1");
    File f = LittleFS.open(DATALOG_PATH, "w");
    if (f) { f.println(telemetry_csv_header()); f.close(); }
    Serial.println(F("  [datalog] rotated log"));
}

}  // namespace datalog
