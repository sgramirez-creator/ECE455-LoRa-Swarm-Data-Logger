#include "PowerManager.h"
#include "config.h"
#include "board.h"
#include "sensors/RtcSensor.h"
#include <Arduino.h>

namespace power {

static volatile bool s_woke = false;

static void onRtcInt() { s_woke = true; }

void begin() {
#if SENSOR_RAIL_EN_PIN >= 0
    pinMode(SENSOR_RAIL_EN_PIN, OUTPUT);
    sensorRail(true);
#endif
    pinMode(RTC_INT_PIN, INPUT_PULLUP);   // RV-3028 INT is open-drain, active low
    attachInterrupt(digitalPinToInterrupt(RTC_INT_PIN), onRtcInt, FALLING);
}

void sensorRail(bool on) {
#if SENSOR_RAIL_EN_PIN >= 0
    bool level = SENSOR_RAIL_ACTIVE_HIGH ? on : !on;
    digitalWrite(SENSOR_RAIL_EN_PIN, level ? HIGH : LOW);
    if (on) delay(50);   // let rails settle before the bus is used
#else
    (void)on;
#endif
}

uint32_t sleepUntilWake(RtcSensor& rtc, uint32_t maxMs) {
#if !FEATURE_SLEEP
    delay(maxMs);
    return maxMs;
#else
    // Quiesce peripherals we control.
    board::statusLed(false);
    board::commsLed(false);
#if TRANSPORT_MESHTASTIC
    MESH_UART.flush();
    MESH_UART.end();
#endif
    sensorRail(false);
    Serial.flush();

    s_woke = false;
    rtc.clearTimerFlag();          // clear any stale edge before we wait

    uint32_t t0 = millis();
    // __wfi() halts the core until the next interrupt (RTC INT, systick, USB,
    // ...); the loop re-checks the wake flag and the backstop timeout.
    while (!s_woke && (millis() - t0) < maxMs) {
        __wfi();
    }
    uint32_t slept = millis() - t0;

    rtc.clearTimerFlag();
#if TRANSPORT_MESHTASTIC
    MESH_UART.begin(MESH_UART_BAUD);
#endif
    sensorRail(true);
    return slept;
#endif
}

}  // namespace power
