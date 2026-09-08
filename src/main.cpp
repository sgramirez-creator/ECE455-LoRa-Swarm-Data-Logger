#include <Arduino.h>

#include "config.h"
#include "board.h"
#include "scheduler.h"
#include "telemetry.h"
#include "Validator.h"
#include "PowerManager.h"
#include "DataLog.h"
#include "sensors/SensorManager.h"
#include "sensors/RtcSensor.h"
#include "sensors/BME680Sensor.h"
#include "sensors/GNSSSensor.h"
#include "sensors/TempString.h"
#include "sensors/ModbusSonde.h"
#include "sensors/WeatherStation.h"
#include "sensors/RainGauge.h"

#if TRANSPORT_MESHTASTIC
#include "MeshLink.h"
#endif
#if TRANSPORT_LORA_P2P
#include "LoRaLink.h"
#endif

// ---- Subsystems -----------------------------------------------------------
static Scheduler      schedule(MEASUREMENT_INTERVAL_MS);
static SensorManager  sensors;

static RtcSensor      rtc;
static BME680Sensor   bme680;
static GNSSSensor     gnss;
static TempString     tempString;
static ModbusSonde    sonde;
static WeatherStation weather;
static RainGauge      rain;

static uint16_t g_seq = 0;
static bool     uplinkReady = false;

// ---- Time source --------------------------------------------------------
static uint32_t nowEpoch() {
#if FEATURE_RTC
    uint32_t e = rtc.epoch();
    if (e >= 1672531200UL) return e;
#endif
    return 0;   // 0 => scheduler runs free-running on millis()
}

// ---- One measurement cycle ----------------------------------------------
static void runMeasurementCycle() {
    board::statusLed(true);
    power::sensorRail(true);

    TelemetryRecord rec;
    telemetry_clear(rec);
    rec.node_id  = NODE_ID;
    rec.seq      = g_seq++;
    rec.uptime_s = millis() / 1000UL;
    rec.battery_v = board::batteryVolts();
    rec.solar_v   = board::solarVolts();

    bool critical = !isnan(rec.battery_v) && rec.battery_v < BATTERY_CRITICAL_V;
    if (!isnan(rec.battery_v) && rec.battery_v < BATTERY_LOW_V)
        rec.flags |= TELEMETRY_FLAG_LOW_BATTERY;

    sensors.sample(rec);

#if FEATURE_RTC && FEATURE_GNSS
    if (gnss.lastEpoch()) rtc.syncTo(gnss.lastEpoch());
#endif

    validate::check(rec);

    char line[512];
    telemetry_to_csv(rec, line, sizeof(line));
    Serial.printf("[cycle %lu seq %u] %s\n",
                  schedule.cycleCount(), rec.seq, line);

#if FEATURE_DATALOG
    datalog::rotateIfNeeded();
    if (!datalog::append(rec)) rec.flags |= TELEMETRY_FLAG_SD_ERROR;
#endif

    if (critical) {
        Serial.println(F("  [power] battery critical -> logged only, no transmit"));
    } else if (uplinkReady) {
#if TRANSPORT_MESHTASTIC
        meshlink::sendRecord(rec);
#elif TRANSPORT_LORA_P2P
        loralink::sendRecord(rec);
#endif
    }

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

    power::begin();

    Serial.println(F("[init] transport"));
#if TRANSPORT_MESHTASTIC
    uplinkReady = meshlink::begin();
#elif TRANSPORT_LORA_P2P
    uplinkReady = loralink::begin();
#endif

#if FEATURE_RTC
    rtc.armPeriodicTimer(MEASUREMENT_INTERVAL_MS / 1000UL);
#endif

#if FEATURE_SAFETY_LIGHT
    board::safetyLight(true);   // on whenever the node is powered
#endif

    schedule.begin(/*runAtStart=*/true);
    Serial.println(F("[init] done\n"));
}

void loop() {
#if TRANSPORT_LORA_P2P
    if (uplinkReady) loralink::poll();
#if FEATURE_LORA_SMOKETEST
    static uint32_t nextHello = 0;
    if (uplinkReady && (int32_t)(millis() - nextHello) >= 0) {
        static uint32_t seq = 0;
        loralink::sendHello(seq++);
        board::commsLed(true); delay(5); board::commsLed(false);
        nextHello = millis() + LORA_SMOKETEST_INTERVAL_MS;
    }
#endif
#endif

    if (schedule.due(nowEpoch())) runMeasurementCycle();

#if FEATURE_SLEEP && !(TRANSPORT_LORA_P2P && FEATURE_LORA_SMOKETEST)
    uint32_t ms = schedule.msUntilNext(nowEpoch());
    if (ms > 3000) {
        uint32_t slept = power::sleepUntilWake(rtc, ms - 1000);
        Serial.printf("[sleep] %lu ms\n", (unsigned long)slept);
    } else {
        delay(20);
    }
#else
    delay(20);
#endif
}
