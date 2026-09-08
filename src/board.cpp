#include "board.h"
#include "config.h"
#include "telemetry.h"
#include <Arduino.h>
#include <math.h>

namespace board {

void begin() {
    pinMode(PIN_LED1, OUTPUT);
    pinMode(PIN_LED2, OUTPUT);
    statusLed(false);
    commsLed(false);

#if FEATURE_SAFETY_LIGHT
    pinMode(SAFETY_LIGHT_PIN, OUTPUT);
    safetyLight(false);
#endif

    analogReadResolution(12);
}

void statusLed(bool on) { digitalWrite(PIN_LED1, on ? HIGH : LOW); }
void commsLed(bool on)  { digitalWrite(PIN_LED2, on ? HIGH : LOW); }

void blink(uint8_t times, uint16_t ms) {
    for (uint8_t i = 0; i < times; ++i) {
        statusLed(true);  delay(ms);
        statusLed(false); delay(ms);
    }
}

void safetyLight(bool on) {
#if FEATURE_SAFETY_LIGHT
    bool level = SAFETY_LIGHT_ACTIVE_HIGH ? on : !on;
    digitalWrite(SAFETY_LIGHT_PIN, level ? HIGH : LOW);
#else
    (void)on;
#endif
}

float batteryVolts() {
    int counts = analogRead(BATTERY_ADC_PIN);
    if (counts <= 0) return NAN;
    float v_adc = (counts / BATTERY_ADC_MAX_COUNTS) * BATTERY_ADC_REF_V;
    return v_adc * BATTERY_DIVIDER_RATIO;
}

void printBanner() {
    Serial.println();
    Serial.println(F("=========================================="));
    Serial.println(F(" ECE455 LoRa Swarm Data Logger"));
    Serial.printf ("  node id      : %u\n", (unsigned)NODE_ID);
    Serial.printf ("  firmware     : %s\n", FIRMWARE_VERSION);
    Serial.printf ("  schema ver   : %u\n", (unsigned)TELEMETRY_SCHEMA_VERSION);
    Serial.printf ("  cycle period : %lu s\n", MEASUREMENT_INTERVAL_MS / 1000UL);
    Serial.println(F("=========================================="));
}

}  // namespace board
