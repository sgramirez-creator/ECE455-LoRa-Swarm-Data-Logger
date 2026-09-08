# Phase 3 — Embedded System

Transport decision: **Meshtastic**, companion-bridge architecture (this MCU is a
datalogger; a co-located RAK11310 runs stock Meshtastic). See
[05-meshtastic-node-config.md](05-meshtastic-node-config.md).

Builds clean in both transport configs (`pio run` → SUCCESS; Meshtastic 8.3%
flash / 4.2% RAM, LoRa-P2P 9.8% / 4.6%).

## Checklist

| Task | Status | Where |
|---|---|---|
| Sensor abstraction layer | ✅ Phase 2 | `ISensor` / `SensorManager` |
| Data validation | ✅ | `Validator` — range gates + stuck detection |
| Telemetry structure | ✅ | `TelemetryRecord` **schema v3** (+`seq`); CSV + compact JSON + base64 |
| Local data storage | ✅ | `DataLog` — LittleFS CSV, rotates at 400 KB, written before every transmit |
| 30-minute scheduler | ✅ | `Scheduler` — epoch-aligned to :00/:30 wall-clock, millis fallback |
| Low-power sleep/wake | ⚠️ partial | `PowerManager` — RTC-timer wake + `__wfi` light sleep; **deep sleep is a TODO** |

## New this phase

### Data validation — `Validator.cpp`
`validate::check(rec)` runs after `SensorManager::sample()` and before log/TX:
- **Range gates** (bounds in `config.h`): a present value outside `[min,max]` is
  set to NaN and `TELEMETRY_FLAG_SENSOR_FAULT` is raised.
- **Wind direction** wrapped into `[0,360)`.
- **(0,0) GNSS fix** — the classic "no real fix" artefact — is nulled and the
  `GNSS_FIX` flag cleared.
- **Stuck detection**: a bit-identical value for `VALIDATE_STUCK_CYCLES` (6)
  consecutive cycles on a live sensor flags a fault (not nulled — could be real).

### Telemetry structure
- Added `uint16_t seq` (wrapping cycle counter) for Phase 4 packet-loss stats.
- `telemetry_to_json()` — short keys, NaN fields omitted, kept under Meshtastic's
  ~228-byte text limit; `MeshLink` falls back to base64 of the packed struct
  (117 B → 156 chars) when JSON would overflow.
- Wire/log/mesh formats all key off `schema_version` so the server decodes any
  firmware rev.

### Epoch-aligned scheduler
`Scheduler::due(epochNow)` fires when `epochNow / intervalSeconds` increments —
i.e. on real :00 / :30 boundaries — so every node samples at the same instants
(important for a spatial swarm and for mesh airtime: staggering is handled by
Meshtastic's CSMA, alignment keeps data comparable). Until the RTC has valid
time it free-runs on `millis()`.

### Sleep / wake — `PowerManager.cpp`
- **Wake source**: `RtcSensor::armPeriodicTimer()` programs the RV-3028 periodic
  countdown timer (1 Hz, 1800 s, auto-reload) to pull `RTC_INT_PIN` low. A GPIO
  interrupt on that pin sets the wake flag.
- **Between cycles**: LEDs off, `MESH_UART` flushed + `end()`, sensor rail
  (`SENSOR_RAIL_EN_PIN`) cut, then `__wfi()` loop until the RTC edge or a
  `millis()` backstop (so a miswired RTC degrades power, never hangs).
- **Backstop**: `loop()` sleeps for `msUntilNext() - 1 s`, then the scheduler
  fires the aligned cycle.

**Limitation — RP2040 power.** `__wfi()` halts the core but clocks + regulator
stay up: this is ~mA, not µA. True RP2040 dormant mode (wake on GPIO edge,
~180 µA) needs `pico-extras` or register-level clock teardown — a TODO in
`PowerManager.cpp`. Per [01-open-source-lora-systems.md](01-open-source-lora-systems.md),
if the 6–7 month budget doesn't close on RP2040 the companion MCU should be an
**nRF52840 or STM32L0** — the `PowerManager` API is the only thing that changes.

## Power path still to build (Phase 3 tail / hardware)

- Real `SENSOR_RAIL_EN_PIN` load switch for GNSS + RS485 + sonde (currently −1).
- GNSS hot-fix / backup battery so a position fix isn't a multi-minute wake.
- Confirm `RTC_INT_PIN`, `SENSOR_RAIL_EN_PIN` against the RAK12002 + base schematic.
- Consider PWM'ing the safety light (duty < 100%) to cut its always-on draw.
- Measure actual sleep current and project the battery/solar budget.
