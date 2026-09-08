#pragma once
//
// Central build-time configuration for the LoRa Swarm Data Logger.
// Everything here is a compile-time constant so a node's behaviour is fully
// described by this file plus the wiring.
//

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------
#define NODE_ID              1          // unique per deployed node
#define FIRMWARE_VERSION     "0.1.0"

// ---------------------------------------------------------------------------
// Measurement schedule
// ---------------------------------------------------------------------------
// Project goal: one measurement cycle every 30 minutes.
// Keep this short (e.g. 60 s) during bench bring-up.
#define MEASUREMENT_INTERVAL_MS   (30UL * 60UL * 1000UL)

// How long sensors are allowed to warm up before a reading is taken.
#define SENSOR_WARMUP_MS          (3UL * 1000UL)

// ---------------------------------------------------------------------------
// Radio (SX1262 / 915 MHz US band)
// ---------------------------------------------------------------------------
#define LORA_FREQUENCY_MHZ        915.0
#define LORA_BANDWIDTH_KHZ        125.0
#define LORA_SPREADING_FACTOR     10
#define LORA_CODING_RATE          5        // 4/5
#define LORA_SYNC_WORD            0x34     // private network
#define LORA_TX_POWER_DBM         22       // SX1262 max; respect local duty-cycle rules
#define LORA_PREAMBLE_LEN         8
#define LORA_TCXO_VOLTAGE         1.8      // RAK11310 TCXO is fed from DIO3

// Bench smoke-test: transmit a hello packet every N ms from setup-loop.
#define LORA_SMOKETEST_INTERVAL_MS  (10UL * 1000UL)

// ---------------------------------------------------------------------------
// Local storage
// ---------------------------------------------------------------------------
#define DATALOG_PATH            "/telemetry.csv"
#define DATALOG_MAX_BYTES       (400UL * 1024UL)   // rotate before filling the FS

// ---------------------------------------------------------------------------
// Hardware pins not covered by the board variant
// ---------------------------------------------------------------------------
// Visible safety light (external high-current LED driver). Placeholder — set to
// the real WisBlock IO pin once the carrier board is finalised.
#define SAFETY_LIGHT_PIN       WB_IO1
#define SAFETY_LIGHT_ACTIVE_HIGH  1

// Battery sense: RAK11310 routes VBAT through a divider to ADC0 (GP26 / WB_A0).
#define BATTERY_ADC_PIN        WB_A0
#define BATTERY_DIVIDER_RATIO  3.0f        // R17 200k / R18 100k -> (200k+100k)/100k
#define BATTERY_ADC_REF_V      3.3f
#define BATTERY_ADC_MAX_COUNTS 4095.0f

// ---------------------------------------------------------------------------
// Feature flags — flip these off to isolate subsystems during bring-up
// ---------------------------------------------------------------------------
#define FEATURE_RADIO          1
#define FEATURE_DATALOG        1
#define FEATURE_SAFETY_LIGHT   1
#define FEATURE_LORA_SMOKETEST 1   // periodic hello packet instead of real telemetry
