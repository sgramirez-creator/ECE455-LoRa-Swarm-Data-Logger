# Firmware Architecture & Phase 1 Status

**Target:** RAK11310 (RP2040 + SX1262) · earlephilhower Arduino-Pico core · PlatformIO

## Phase 1 checklist

| Task | Status | Notes |
|---|---|---|
| Create PlatformIO project | ✅ | Pre-existing; cleaned up `platformio.ini` |
| Configure RAK11310 | ✅ | `board = rakwireless_rak11300`, earlephilhower core; verified pin map from RAK datasheet + BSP variant (see header of `platformio.ini`) |
| Verify C++ firmware builds | ✅ | `pio run` → SUCCESS, ~6% flash, ~4% RAM, RadioLib linked |
| Verify serial communication | ⏳ hardware | Firmware prints a banner + per-cycle CSV at 115200; assumed-correct pending hardware |
| Verify RAK19001 / RAK11310 hardware | ⏳ hardware | assumed-correct pending hardware |

## Module layout

```
include/
  config.h              all build-time tunables + feature flags (one file = one node's behaviour)
  telemetry.h           TelemetryRecord: packed, versioned measurement struct (schema v2) — the shared contract
  board.h               LEDs, safety light, battery sense, boot banner
  scheduler.h           fixed-interval cycle trigger (millis now; RTC-alarm later)
  DataLog.h             append-only CSV log on LittleFS (internal flash)
  MeshLink.h            Meshtastic Serial-Module bridge (default transport)
  LoRaLink.h            SX1262 P2P transport (bench / TRANSPORT_LORA_P2P only)
  Validator.h           plausibility gates run on each completed record
  PowerManager.h        sensor-rail switching + sleep/wake (RTC-timer wake)
  Rs485Bus.h            shared half-duplex RS485/Modbus UART (RAK5802)
  sensors/
    ISensor.h           abstract sensor driver interface (ModularSensors-style)
    SensorManager.h     registry + one-cycle orchestration (overlapped warmup)
    RtcSensor.h         RAK12002 RV-3028-C7 — epoch + (phase 3) wake alarm
    BME680Sensor.h      RAK1906 — Adafruit BME680
    GNSSSensor.h        RAK12500 — SparkFun u-blox v2 (ZOE-M8Q)
    TempString.h        DS18B20 1-Wire chain (≤ 8), vertical profile
    ModbusSonde.h       water-quality sonde, Modbus RTU over RS485
    WeatherStation.h    integrated met station, Modbus RTU over RS485
    RainGauge.h         tipping-bucket, pin-change interrupt counter
src/                    matching .cpp for each of the above + main.cpp
```

See [03-phase2-sensors.md](03-phase2-sensors.md) for the sensor drivers, the
sampling-priority order, and the config assumptions that need confirming against
real hardware.

### Data flow (one measurement cycle)

```
Scheduler.due()
  └─> telemetry_clear(rec); rec.node_id / uptime / battery_v
      └─> SensorManager.sample(rec)         powerUp → startMeasurement → warmup → read → powerDown
          └─> datalog::append(rec)          CSV line to /telemetry.csv (rotates at 400 KB)
              └─> loralink::sendRecord(rec)  raw packed struct over LoRa
```

`main.cpp` also runs a **LoRa smoke-test beacon** (`FEATURE_LORA_SMOKETEST`) — a short
`SWARM node=N seq=M` packet every 10 s so a second node or an SDR can confirm the RF path
in Phase 1/4 before real telemetry exists.

## Key design decisions

- **`TelemetryRecord` is the single contract.** Sensors fill it, the logger persists it, the
  radio serialises it. Add fields at the end and bump `TELEMETRY_SCHEMA_VERSION` so a ground
  station can decode any firmware revision.
- **Sensor layer mirrors EnviroDIY ModularSensors** (`ISensor`: `begin/powerUp/startMeasurement/warmupMs/read/powerDown`). Phase 2 = replace the two stubs with real drivers and add the water-quality / rain / weather / RS-485 sensors behind the same interface. Absent sensors are detected at boot and skipped.
- **Transport is abstracted behind `LoRaLink`.** Whether the node ends up running Meshtastic
  (custom module), LoRaWAN (ChirpStack), or plain P2P is still open — see
  [01-open-source-lora-systems.md](01-open-source-lora-systems.md) §6. The rest of the
  firmware only calls `sendRecord()`.
- **Scheduler is time-source agnostic** so Phase 3 can swap `millis()` for an RTC alarm and
  put the MCU in deep sleep for `msUntilNext()`.

## Verified RAK11310 pin map (from RAK datasheet + earlephilhower BSP)

| Function | Pins |
|---|---|
| `Wire` (I2C1) — base-board sensor slots | SDA GP2 / SCL GP3 |
| `Wire1` (I2C0) — WisBlock IO slot | SDA GP20 / SCL GP21 |
| `Serial1` (UART0) | TX GP0 / RX GP1 |
| `Serial2` (UART1) — RS-485 / weather candidate | TX GP4 / RX GP5 |
| `SPI1` → SX1262 (hardwired) | SCK GP10 / MOSI GP11 / MISO GP12 / NSS GP13 |
| SX1262 control | NRESET GP14 · BUSY GP15 · DIO1 GP29 · ANT_PWR GP25 · TCXO on DIO3 @1.8 V · DIO2 = RF switch |
| Status LEDs | green GP23 / blue GP24 |
| Battery ADC | GP26 (ADC0) via ~3:1 divider |

⚠️ `WB_IO1..6` in `platformio.ini` are inherited values — confirm against the RAK19001
schematic before wiring the safety light and external peripherals.

## Build / flash

```
pio run                 # build
pio run -t upload        # drag-and-drop UF2, or 1200bps-touch auto-reset
pio device monitor       # 115200
```

## Phase 2 status — sensor integration ✅

Eight `ISensor` drivers with boot-time presence detection —
[03-phase2-sensors.md](03-phase2-sensors.md).

## Phase 3 status — embedded system ✅ (sleep partial)

Transport = **Meshtastic**, companion-bridge. Data validation, schema-v3
telemetry, epoch-aligned scheduler, RTC-timer wake. Deep-sleep to µA on RP2040
is the open item. Details: [04-phase3-embedded.md](04-phase3-embedded.md),
node setup: [05-meshtastic-node-config.md](05-meshtastic-node-config.md).

## Next (Phase 4 — Meshtastic bring-up & range)

1. Flash stock Meshtastic on the RAK11310, wire + configure the Serial Module.
2. Stand up the gateway → MQTT → Node-RED → InfluxDB → Grafana path (beegee-tokyo).
3. Node-to-node, multi-hop, range + packet-loss tests (`seq`/`id` give loss stats).
4. Close the power budget; decide RP2040 vs nRF52 companion.
