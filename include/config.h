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

// Align measurement cycles to wall-clock boundaries (:00, :30, ...) using RTC
// time, so every node in the swarm samples together. Falls back to a
// free-running millis() interval until the RTC has valid time.
#define SCHEDULE_EPOCH_ALIGNED    1

// ---------------------------------------------------------------------------
// Transport — how telemetry leaves the node
// ---------------------------------------------------------------------------
// TRANSPORT_MESHTASTIC: this MCU is a companion datalogger. The RAK11310 next
//   to it runs stock Meshtastic; we hand each record to its Serial Module over
//   a UART. See docs/05-meshtastic-node-config.md.
// TRANSPORT_LORA_P2P: this MCU drives the SX1262 directly (bench / no-mesh).
// Override from a PlatformIO env with -D if needed.
#ifndef TRANSPORT_MESHTASTIC
#define TRANSPORT_MESHTASTIC      1
#endif
#ifndef TRANSPORT_LORA_P2P
#define TRANSPORT_LORA_P2P        0
#endif

// --- Meshtastic Serial Module bridge ---
#define MESH_UART                 Serial2      // UART1 GP4/GP5 on an RP2040 companion
#define MESH_UART_BAUD            38400        // must match Meshtastic serial.baud
#define MESH_MSG_PREFIX          "SWARM "      // gateway/Node-RED filters on this
#define MESH_UART_QUIET_MS        300          // idle gap that frames a packet (> serial.timeout)

// ---------------------------------------------------------------------------
// Radio (SX1262 / 915 MHz US band) — used only when TRANSPORT_LORA_P2P
// ---------------------------------------------------------------------------
#define LORA_FREQUENCY_MHZ        915.0
#define LORA_BANDWIDTH_KHZ        125.0
#define LORA_SPREADING_FACTOR     10
#define LORA_CODING_RATE          5        // 4/5
#define LORA_SYNC_WORD            0x34     // private network
#define LORA_TX_POWER_DBM         22       // SX1262 max; respect local duty-cycle rules
#define LORA_PREAMBLE_LEN         8
#define LORA_TCXO_VOLTAGE         1.8      // RAK11310 TCXO is fed from DIO3

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
#define BATTERY_LOW_V          3.45f       // sets TELEMETRY_FLAG_LOW_BATTERY
#define BATTERY_CRITICAL_V     3.30f       // skip transmit, log only

// Optional solar-panel voltage sense (ADC1 / GP27 / WB_A1). -1 to disable.
#define SOLAR_ADC_PIN         (-1)
#define SOLAR_DIVIDER_RATIO   3.0f

// RV-3028 INT pin -> MCU wake source for deep sleep (open-drain, active low).
// VERIFY against the RAK12002 + base schematic.
#define RTC_INT_PIN           WB_IO3

// Switched power rail for GNSS + RS485 + sonde (load switch, active high).
// -1 leaves them permanently powered.
#define SENSOR_RAIL_EN_PIN   (-1)
#define SENSOR_RAIL_ACTIVE_HIGH 1

// ---------------------------------------------------------------------------
// Sensors — Phase 2
// ---------------------------------------------------------------------------
// I2C addresses (all on `Wire` = i2c1, GP2/GP3 = WisBlock base sensor slots).
#define BME680_I2C_ADDR         0x76      // RAK1906; 0x77 on some breakouts
#define RV3028_I2C_ADDR         0x52      // fixed for RV-3028-C7 (RAK12002)
#define UBLOX_I2C_ADDR          0x42      // RAK12500 default

// --- DS18B20 temperature string (1-Wire) ---
#define TEMP_STRING_ONEWIRE_PIN     WB_IO6
#define TEMP_STRING_RESOLUTION_BITS 12
// Sensors are reported in discovery order. Swap for address-sorted order once
// the physical top-to-bottom addresses are known (see docs/03-phase2-sensors.md).

// --- Tipping-bucket rain gauge (reed switch to GND) ---
#define RAIN_GAUGE_PIN         WB_IO5
#define RAIN_MM_PER_TIP        0.2794f   // 0.011" — typical; calibrate per gauge
#define RAIN_DEBOUNCE_MS       80

// --- RS485 bus (RAK5802) shared by the sonde + weather station ---
// VERIFY the UART routing and DE pin against the RAK19001 + RAK5802 schematic.
#define RS485_SERIAL           Serial1   // UART0 GP0/GP1 on RAK11310
#define RS485_BAUD             9600
#define RS485_DE_PIN           WB_IO2    // driver-enable / direction control (-1 if auto)
#define RS485_CONFIG           SERIAL_8N1

// --- Water-quality sonde (Modbus RTU) ---
// Holding-register map. Each value is a big-endian float32 (2 registers).
// Set a register to -1 to skip that parameter. ADJUST to your sonde's manual.
#define SONDE_MODBUS_ID        1
#define SONDE_REG_TEMP_C       0
#define SONDE_REG_PH           2
#define SONDE_REG_DO_MGL       4
#define SONDE_REG_COND_USCM    6
#define SONDE_REG_TURB_NTU     8
#define SONDE_REG_FUNCTION     3         // 3 = holding registers, 4 = input registers

// --- Integrated weather station (Modbus RTU) ---
#define WX_MODBUS_ID           2
#define WX_REG_WIND_SPEED_MS   0
#define WX_REG_WIND_DIR_DEG    2
#define WX_REG_AIR_TEMP_C      -1        // -1 = rely on the BME680 instead
#define WX_REG_AIR_RH_PCT      -1
#define WX_REG_AIR_PRESS_HPA   -1
#define WX_REG_FUNCTION        3

// ---------------------------------------------------------------------------
// Data validation — plausibility gates. A reading outside [min,max] is nulled
// (set to NaN) and TELEMETRY_FLAG_SENSOR_FAULT is raised. STUCK/JUMP checks run
// against the previous cycle.
// ---------------------------------------------------------------------------
#define VALIDATE_ENABLE          1
#define VALIDATE_AIR_T_MIN       (-50.0f)
#define VALIDATE_AIR_T_MAX       ( 65.0f)
#define VALIDATE_AIR_RH_MIN      (  0.0f)
#define VALIDATE_AIR_RH_MAX      (100.0f)
#define VALIDATE_AIR_P_MIN       (800.0f)
#define VALIDATE_AIR_P_MAX       (1100.0f)
#define VALIDATE_WATER_T_MIN     (-5.0f)
#define VALIDATE_WATER_T_MAX     (60.0f)
#define VALIDATE_PH_MIN          (0.0f)
#define VALIDATE_PH_MAX          (14.0f)
#define VALIDATE_DO_MIN          (0.0f)
#define VALIDATE_DO_MAX          (25.0f)
#define VALIDATE_COND_MIN        (0.0f)
#define VALIDATE_COND_MAX        (200000.0f)
#define VALIDATE_TURB_MIN        (0.0f)
#define VALIDATE_TURB_MAX        (4000.0f)
#define VALIDATE_WIND_MS_MIN     (0.0f)
#define VALIDATE_WIND_MS_MAX     (100.0f)
#define VALIDATE_RAIN_MM_MAX     (200.0f)          // per cycle
#define VALIDATE_LAT_ABS_MAX     (90.0f)
#define VALIDATE_LON_ABS_MAX     (180.0f)
// Stuck detection: N identical consecutive cycles on a live sensor is suspicious.
#define VALIDATE_STUCK_CYCLES    6

// ---------------------------------------------------------------------------
// Feature flags — flip these off to isolate subsystems during bring-up
// ---------------------------------------------------------------------------
#define FEATURE_DATALOG        1
#define FEATURE_SAFETY_LIGHT   1
#define FEATURE_SLEEP          1   // enter low-power sleep between cycles

// Bench LoRa smoke-test (TRANSPORT_LORA_P2P only): periodic hello packet.
#define FEATURE_LORA_SMOKETEST 1
#define LORA_SMOKETEST_INTERVAL_MS  (10UL * 1000UL)

#define FEATURE_BME680         1
#define FEATURE_RTC            1
#define FEATURE_GNSS           1
#define FEATURE_RAIN_GAUGE     1
#define FEATURE_TEMP_STRING    1
#define FEATURE_SONDE          1
#define FEATURE_WEATHER        1
