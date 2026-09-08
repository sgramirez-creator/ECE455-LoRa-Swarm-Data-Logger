#include "sensors/RainGauge.h"
#include "config.h"
#include <Arduino.h>

static volatile uint32_t s_tips = 0;
static volatile uint32_t s_lastTipMs = 0;

static void onTip() {
    uint32_t now = millis();
    if (now - s_lastTipMs >= RAIN_DEBOUNCE_MS) {
        s_tips++;
        s_lastTipMs = now;
    }
}

bool RainGauge::begin() {
    pinMode(RAIN_GAUGE_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(RAIN_GAUGE_PIN), onTip, FALLING);
    s_tips = 0;
    // No bus to probe — presence is a wiring assumption, so report OK.
    return true;
}

uint32_t RainGauge::tipsSinceReset() const {
    noInterrupts();
    uint32_t t = s_tips;
    interrupts();
    return t;
}

bool RainGauge::read(TelemetryRecord& out) {
    noInterrupts();
    uint32_t t = s_tips;
    s_tips = 0;
    interrupts();

    out.rain_mm = t * RAIN_MM_PER_TIP;
    return true;
}
