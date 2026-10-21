#pragma once

#include "configuration.h"

#if defined(HAS_GNSS_I2C) && !MESHTASTIC_EXCLUDE_GPS

#include "TinyGPS++.h"
#include "concurrency/OSThread.h"

/**
 * Reads a u-blox receiver (RAK12500 / ZOE-M8Q) over its I2C (DDC) port and feeds fixes into NodeDB and the RTC.
 * Keeps the GNSS off every UART so RS485 owns Serial1 exclusively.
 */
class GnssI2CModule : private concurrency::OSThread
{
  public:
    GnssI2CModule();

    bool hasFix() const { return fixValid; }

  protected:
    int32_t runOnce() override;

  private:
    TinyGPSPlus reader;
    bool present = false;
    bool fixValid = false;
    bool publishedOnce = false;
    bool clockSetOnce = false;
    uint8_t busErrors = 0;
    uint32_t lastPublishMs = 0;
    uint32_t lastClockSetMs = 0;

    bool probe();
    int32_t bytesAvailable();
    bool drain();
    bool hasFreshLock();
    void maybeSetClock();
    void publish();
};

extern GnssI2CModule *gnssI2CModule;

#endif
