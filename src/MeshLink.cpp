#include "MeshLink.h"
#include "config.h"
#include <Arduino.h>

namespace meshlink {

// Meshtastic text payload ceiling (Constants.DATA_PAYLOAD_LEN 237, minus margin).
static constexpr int MESH_TEXT_MAX = 228;

static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64encode(const uint8_t* in, int n, char* out) {
    int o = 0;
    for (int i = 0; i < n; i += 3) {
        uint32_t v = in[i] << 16;
        if (i + 1 < n) v |= in[i + 1] << 8;
        if (i + 2 < n) v |= in[i + 2];
        out[o++] = B64[(v >> 18) & 0x3F];
        out[o++] = B64[(v >> 12) & 0x3F];
        out[o++] = (i + 1 < n) ? B64[(v >> 6) & 0x3F] : '=';
        out[o++] = (i + 2 < n) ? B64[v & 0x3F] : '=';
    }
    out[o] = 0;
    return o;
}

bool begin() {
    MESH_UART.begin(MESH_UART_BAUD);
    // No reliable way to probe the Meshtastic node; assume it is wired.
    Serial.printf("  [mesh] serial bridge on %s @ %lu baud\n",
                  "MESH_UART", (unsigned long)MESH_UART_BAUD);
    return true;
}

static bool emit(const char* prefix, const char* body) {
    int total = (int)strlen(prefix) + (int)strlen(body);
    if (total > MESH_TEXT_MAX) {
        Serial.printf("  [mesh] payload %d > %d, dropped\n", total, MESH_TEXT_MAX);
        return false;
    }
    MESH_UART.print(prefix);
    MESH_UART.print(body);
    MESH_UART.print('\n');
    MESH_UART.flush();
    delay(MESH_UART_QUIET_MS);   // idle gap frames the packet for the Serial Module
    Serial.printf("  [mesh] TX %s%s\n", prefix, body);
    return true;
}

bool sendRecord(const TelemetryRecord& r) {
    char json[400];
    int n = telemetry_to_json(r, json, sizeof(json));

    if (n > 0 && (int)strlen(MESH_MSG_PREFIX) + n <= MESH_TEXT_MAX)
        return emit(MESH_MSG_PREFIX, json);

    // Fall back to base64 of the packed struct.
    char b64[200];
    b64encode((const uint8_t*)&r, (int)sizeof(r), b64);
    return emit("SWARM64 ", b64);
}

}  // namespace meshlink
