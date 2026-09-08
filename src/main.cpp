#include <Arduino.h>

#include "config.h"
#include "board.h"
#include "scheduler.h"
#include "telemetry.h"
#include "DataLog.h"
#include "LoRaLink.h"
#include "sensors/SensorManager.h"
#include "sensors/RtcSensor.h"
#include "sensors/BME680Sensor.h"
#include "sensors/GNSSSensor.h"
#include "sensors/TempString.h"
#include "sensors/ModbusSonde.h"
#include "sensors/WeatherStation.h"
#include "sensors/RainGauge.h"

// ---- Subsystems -----------------------------------------------------------
static Scheduler      schedule(MEASUREMENT_INTERVAL_MS);
static SensorManager  sensors;

// Registration order defines sampling order: RTC first (time), then GNSS can
// override with satellite time, then the environmental / water sensors.
static RtcSensor      rtc;
static BME680Sensor   bme680;
static GNSSSensor     gnss;
static TempString     tempString;
static ModbusSonde    sonde;
static WeatherStation weather;
static RainGauge      rain;

static bool radioReady = false;

// ---- One measurement cycle ----------------------------------------------
static void runMeasurementCycle() {
    board::statusLed(true);

    TelemetryRecord rec;
    telemetry_clear(rec);
    rec.node_id  = NODE_ID;
    rec.uptime_s = millis() / 1000UL;
    rec.battery_v = board::batteryVolts();
    if (!isnan(rec.battery_v) && rec.battery_v < 3.4f)
        rec.flags |= TELEMETRY_FLAG_LOW_BATTERY;

    sensors.sample(rec);

    // Discipline the RTC from GNSS time when we have it.
#if FEATURE_RTC && FEATURE_GNSS
    if (gnss.lastEpoch()) rtc.syncTo(gnss.lastEpoch());
#endif

    char line[512];
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
#if FEATURE_RTC
    sensors.add(&rtc);
#endif
#if FEATURE_BME680
    sensors.add(&bme680);
#endif
#if FEATURE_GNSS
    sensors.add(&gnss);
#endif
#if FEATURE_TEMP_STRING
    sensors.add(&tempString);
#endif
#if FEATURE_SONDE
    sensors.add(&sonde);
#endif
#if FEATURE_WEATHER
    sensors.add(&weather);
#endif
#if FEATURE_RAIN_GAUGE
    sensors.add(&rain);
#endif
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
