#pragma once
#include <stdint.h>
class RtcSensor;

//
// Low-power sleep/wake for the datalogger MCU.
//
// The wake source is the RV-3028 periodic countdown timer pulling RTC_INT_PIN
// low (see RtcSensor::armPeriodicTimer). A millis() timeout is kept as a
// backstop so a missing/miswired RTC only degrades power, never hangs the node.
//
// NOTE: the current RP2040 implementation is a __wfi() light sleep — it halts
// the core between interrupts but the clocks and regulator stay up (~mA, not
// µA). True RP2040 dormant mode (wake on GPIO edge, ~180 µA) needs pico-extras
// or register-level code and is a TODO. If the 6-7 month budget does not close
// on RP2040, the companion MCU should be an nRF52/STM32L0 — only this file
// changes. See docs/04-phase3-embedded.md.
//
namespace power {

void begin();

// Switch the GNSS / RS485 / sonde power rail (SENSOR_RAIL_EN_PIN). No-op if -1.
void sensorRail(bool on);

// Sleep until the RTC timer fires or `maxMs` elapses. Returns ms actually slept.
uint32_t sleepUntilWake(RtcSensor& rtc, uint32_t maxMs);

}  // namespace power
