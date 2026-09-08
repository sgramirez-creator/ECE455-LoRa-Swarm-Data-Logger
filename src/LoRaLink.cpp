#include "LoRaLink.h"
#include "config.h"

#if TRANSPORT_LORA_P2P

#include <Arduino.h>
#include <RadioLib.h>

namespace loralink {

// SX1262 is hardwired to SPI1 on the RAK11310. Pin macros come from the
// earlephilhower "rakwireless_rak11300" variant.
static SX1262 radio = new Module(PIN_SPI1_SS, PIN_SX1262_DIO1,
                                 PIN_SX1262_NRESET, PIN_SX1262_BUSY, SPI1);

static int  state_  = RADIOLIB_ERR_NONE;
static volatile bool rxFlag_ = false;
static bool listening_ = false;

static void onDio1() { rxFlag_ = true; }

int lastState() { return state_; }

static void resumeReceive() {
    state_ = radio.startReceive();
    listening_ = (state_ == RADIOLIB_ERR_NONE);
}

bool begin() {
    SPI1.begin();
    state_ = radio.begin(LORA_FREQUENCY_MHZ,
                         LORA_BANDWIDTH_KHZ,
                         LORA_SPREADING_FACTOR,
                         LORA_CODING_RATE,
                         LORA_SYNC_WORD,
                         LORA_TX_POWER_DBM,
                         LORA_PREAMBLE_LEN,
                         LORA_TCXO_VOLTAGE);
    if (state_ != RADIOLIB_ERR_NONE) {
        Serial.printf("  [lora] begin failed, state=%d\n", state_);
        return false;
    }
    radio.setDio2AsRfSwitch(true);          // RAK11310 antenna RF switch
    radio.setCurrentLimit(140.0);
    radio.setPacketReceivedAction(onDio1);
    resumeReceive();
    Serial.printf("  [lora] SX1262 up @ %.1f MHz SF%d BW%.0f\n",
                  LORA_FREQUENCY_MHZ, LORA_SPREADING_FACTOR, LORA_BANDWIDTH_KHZ);
    return true;
}

static bool transmitBytes(const uint8_t* data, size_t len, const char* what) {
    state_ = radio.transmit(data, len);
    if (state_ != RADIOLIB_ERR_NONE)
        Serial.printf("  [lora] TX %s failed, state=%d\n", what, state_);
    resumeReceive();
    return state_ == RADIOLIB_ERR_NONE;
}

bool sendRecord(const TelemetryRecord& r) {
    return transmitBytes((const uint8_t*)&r, sizeof(r), "record");
}

bool sendHello(uint32_t seq) {
    char msg[48];
    snprintf(msg, sizeof(msg), "SWARM node=%u seq=%lu",
             (unsigned)NODE_ID, (unsigned long)seq);
    bool ok = transmitBytes((const uint8_t*)msg, strlen(msg), "hello");
    if (ok) Serial.printf("  [lora] TX \"%s\"\n", msg);
    return ok;
}

void poll() {
    if (!listening_ || !rxFlag_) return;
    rxFlag_ = false;

    String in;
    int s = radio.readData(in);
    if (s == RADIOLIB_ERR_NONE) {
        Serial.printf("  [lora] RX \"%s\"  RSSI=%.0f dBm  SNR=%.1f dB\n",
                      in.c_str(), radio.getRSSI(), radio.getSNR());
    }
    resumeReceive();
}

}  // namespace loralink

#endif  // TRANSPORT_LORA_P2P
