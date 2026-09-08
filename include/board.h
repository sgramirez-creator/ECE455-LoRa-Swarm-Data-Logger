#pragma once
#include <stdint.h>

//
// Board-level helpers for the RAK11310 node: status LEDs, the visible safety
// light, battery telemetry, and the boot banner.
//
namespace board {

void begin();

// On-module status LEDs (green = GP23, blue = GP24).
void statusLed(bool on);
void commsLed(bool on);
void blink(uint8_t times, uint16_t ms = 120);

// Visible safety light (external LED driver on SAFETY_LIGHT_PIN).
void safetyLight(bool on);

// Battery pack voltage via the on-board divider, in volts. NaN if unavailable.
float batteryVolts();

void printBanner();

}  // namespace board
