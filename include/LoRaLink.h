#pragma once
#include "telemetry.h"

//
// Thin wrapper over the SX1262 for a plain-LoRa (P2P) link. This is the
// Phase 1 / bench-bring-up transport and the fallback if we do not run
// Meshtastic firmware on the node.
//
// The final mesh transport (Meshtastic custom module, or LoRaWAN via ChirpStack)
// is a separate decision — see docs/01-open-source-lora-systems.md. Keeping the
// send path behind this interface means the rest of the firmware does not care
// which one wins.
//
namespace loralink {

// Bring up the radio with the parameters from config.h. Returns false on error.
bool begin();

// Transmit one telemetry record (raw packed struct). Blocking. Returns false on
// TX error. Updates out.last_rssi_dbm is NOT done here (RX-side concern).
bool sendRecord(const TelemetryRecord& r);

// Bench smoke test: send a short identifying string. Returns false on TX error.
bool sendHello(uint32_t seq);

// Non-blocking poll for an inbound packet; prints it to Serial if present.
void poll();

// Last radio state code (RadioLib) for diagnostics.
int lastState();

}  // namespace loralink
