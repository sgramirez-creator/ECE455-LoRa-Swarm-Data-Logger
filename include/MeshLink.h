#pragma once
#include "telemetry.h"

//
// Bridge to a co-located Meshtastic node's Serial Module.
//
// We are wired MCU-TX -> Meshtastic-RX / MCU-RX <- Meshtastic-TX on MESH_UART.
// The Meshtastic node must have: Serial Module enabled, mode TEXTMSG, a channel
// named "serial" with uplink enabled, and serial.baud == MESH_UART_BAUD. Each
// line we emit is broadcast as a text message and (via a gateway) reaches MQTT.
//
// Payload: one line, `MESH_MSG_PREFIX` + compact JSON. If that would exceed the
// Meshtastic text limit it falls back to `SWARM64 ` + base64 of the packed
// TelemetryRecord (full fidelity, fixed ~160 chars).
//
namespace meshlink {

bool begin();
bool sendRecord(const TelemetryRecord& r);

}  // namespace meshlink
