#include <Arduino.h>

#include "config.h"
#include "board.h"
#include "scheduler.h"
#include "telemetry.h"
#include "DataLog.h"
#include "LoRaLink.h"
#include "sensors/SensorManager.h"
#include "sensors/BME680Sensor.h"
#include "sensors/GNSSSensor.h"

// ---- Subsystems -----------------------------------------------------------
static Scheduler      schedule(MEASUREMENT_INTERVAL_MS);
static SensorManager  sensors;

static BME680Sensor   bme680;
static GNSSSensor     gnss;

static bool radioReady = false;

// ---- One measurement cycle ----------------------------------------------
static void runMeasurementCycle() {
    board::statusLed(true);

    TelemetryRecord rec;
    telemetry_clear(rec);
    rec.node_id  = NODE_ID;
    rec.uptime_s = millis() / 1000UL;
    rec.battery_v = board::batteryVolts();

    sensors.sample(rec);

    char line[320];
    telemetry_to_csv(rec, line, sizeof(line));
    Serial.printf("[cycle %lu] %s\n", schedule.cycleCount(), line);

#if FEATURE_DATALOG
    datalog::rotateIfNeeded();
    if (!datalog::append(rec)) rec.flags |= TELEMETRY_FLAG_SD_ERROR;
#endif

#if FEATURE_RADIO
    if (radioReady) loralink::sendRecord(rec);
#endif

    board::statusLed(false);
}

// ---- Arduino entry points ---------------------------------------------
void setup() {
    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 2000) { /* wait briefly for USB */ }

    board::begin();
    board::printBanner();
    board::blink(2);

    Serial.println(F("[init] sensors"));
    sensors.add(&bme680);
    sensors.add(&gnss);
    int healthy = sensors.begin();
    Serial.printf("[init] %d/%d sensors present\n", healthy, sensors.count());

#if FEATURE_DATALOG
    Serial.println(F("[init] datalog"));
    datalog::begin();
#endif

#if FEATURE_RADIO
    Serial.println(F("[init] radio"));
    radioReady = loralink::begin();
#endif

#if FEATURE_SAFETY_LIGHT
    board::safetyLight(true);   // on whenever the node is powered
#endif

    schedule.begin(/*runAtStart=*/true);
    Serial.println(F("[init] done\n"));
}

void loop() {
#if FEATURE_RADIO
    if (radioReady) loralink::poll();
#endif

#if FEATURE_LORA_SMOKETEST
    // Bench bring-up: transmit an identifying beacon on a fast cadence so a
    // second node / SDR can confirm the RF path before real telemetry exists.
    static uint32_t nextHello = 0;
    if (radioReady && (int32_t)(millis() - nextHello) >= 0) {
        static uint32_t seq = 0;
        loralink::sendHello(seq++);
        board::commsLed(true); delay(5); board::commsLed(false);
        nextHello = millis() + LORA_SMOKETEST_INTERVAL_MS;
    }
#endif

    if (schedule.due()) runMeasurementCycle();

    // Phase 3: replace with RTC-alarm deep sleep for schedule.msUntilNext().
    delay(20);
}
