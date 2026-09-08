#pragma once
#include <Arduino.h>

//
// Shared RS485 bus (RAK5802). One half-duplex UART carries every Modbus RTU
// device on the node (sonde, weather station, ...). Direction control is left
// to each Modbus master via the DE pin.
//
namespace rs485 {

// Idempotent: brings up RS485_SERIAL at RS485_BAUD the first time it is called.
void begin();

Stream& stream();

// Driver-enable pin to hand to modbusMaster::begin(), or -1 if not used.
int8_t enablePin();

}  // namespace rs485
