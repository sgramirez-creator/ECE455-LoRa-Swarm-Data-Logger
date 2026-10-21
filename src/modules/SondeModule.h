#pragma once

#include "configuration.h"

#ifdef HAS_SONDE

#include "SinglePortModule.h"
#include "concurrency/OSThread.h"

/**
 * Yosemitech Y4000 multiparameter sonde over Modbus RTU (RAK5802 RS485 on SONDE_SERIAL).
 * Each cycle is a non-blocking state machine so the mesh keeps running while the sonde stabilises:
 * power on -> warm up -> brush -> stabilise -> read values -> read status -> send JSON on PRIVATE_APP -> power off.
 * The JSON keys match ui/receiver.py.
 */
class SondeModule : public SinglePortModule, private concurrency::OSThread
{
  public:
    SondeModule();

  protected:
    int32_t runOnce() override;

  private:
    enum class Step : uint8_t { Boot, Idle, Brush, RequestValues, AwaitValues, RequestStatus, AwaitStatus, Report };
    enum class Reply : uint8_t { Pending, Ok, Failed };

    // Y4000 register order: DO, turbidity, conductivity, pH, temperature, ORP, chlorophyll, BGA.
    static constexpr uint8_t VALUE_COUNT = 8;

    Step step = Step::Boot;
    float values[VALUE_COUNT] = {};
    bool valuesOk = false;
    bool statusOk = false;
    uint8_t status = 0;
    uint8_t attempts = 0;
    uint16_t seq = 0;
    uint32_t requestSentMs = 0;
    uint8_t rx[48] = {};
    size_t rxLen = 0;

    void powerSonde(bool on);
    void sendFrame(uint8_t *frame, size_t lenWithoutCrc);
    void requestRegisters(uint16_t start, uint16_t count);
    void requestBrush();
    Reply pollReply(size_t expectedLen);
    void decodeValues();
    void simulateValues();
    size_t buildJson(char *buf, size_t cap, bool compact);
    void sendReport();
    int32_t msUntilNextCycle();
};

extern SondeModule *sondeModule;

#endif
