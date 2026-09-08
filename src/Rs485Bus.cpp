#include "Rs485Bus.h"
#include "config.h"

namespace rs485 {

static bool started = false;

void begin() {
    if (started) return;
#if RS485_DE_PIN >= 0
    pinMode(RS485_DE_PIN, OUTPUT);
    digitalWrite(RS485_DE_PIN, LOW);   // receive by default
#endif
    RS485_SERIAL.begin(RS485_BAUD, RS485_CONFIG);
    started = true;
}

Stream& stream() { return RS485_SERIAL; }

int8_t enablePin() {
#if RS485_DE_PIN >= 0
    return (int8_t)RS485_DE_PIN;
#else
    return -1;
#endif
}

}  // namespace rs485
