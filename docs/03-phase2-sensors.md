# Phase 2 — Sensor Integration

All eight sensor line items are implemented as `ISensor` drivers and the project
**builds clean** (`pio run` → SUCCESS, ~10% flash, ~5% RAM). No hardware was
available, so each driver is written to **detect its device at boot and mark
itself absent** if the device does not respond — the node runs with whatever
subset is actually wired.

## Status

| Line item | Driver | Library | Bus / pins | Assumed hardware |
|---|---|---|---|---|
| BME680 | `BME680Sensor` | Adafruit BME680 | I2C `Wire` (GP2/3) @ 0x76 | RAK1906 |
| RTC | `RtcSensor` | constiko RV-3028-C7 | I2C `Wire` @ 0x52 | RAK12002 |
| GNSS | `GNSSSensor` | SparkFun u-blox GNSS **v2** | I2C `Wire` @ 0x42 | RAK12500 (ZOE-M8Q) |
| Rain sensor | `RainGauge` | — (pin ISR) | `RAIN_GAUGE_PIN` = WB_IO5 | tipping bucket / reed switch |
| RS485 | `rs485` bus helper | — | `Serial1` + DE `WB_IO2` | RAK5802 |
| Water-quality sonde | `ModbusSonde` | EnviroDIY SensorModbusMaster | RS485, Modbus id 1 | generic Modbus-RTU sonde |
| Temperature string | `TempString` | OneWire + DallasTemperature | 1-Wire `WB_IO6` | DS18B20 chain (≤ 8) |
| Weather station | `WeatherStation` | SensorModbusMaster | RS485, Modbus id 2 | generic Modbus-RTU met station |

## Measurement cycle

`SensorManager::sample()` runs every registered present sensor as
`powerUp → startMeasurement → (overlapped warmup) → read → powerDown`, all
writing into one `TelemetryRecord`. Registration order (in `main.cpp`) sets
priority when two sensors touch the same field:

```
RTC        → epoch_s, RTC_VALID flag
BME680     → air_temp_c, air_humidity_pct, air_pressure_hpa, gas_resistance_kohm
GNSS       → lat/lon/alt, sats, GNSS_FIX flag, epoch_s (overrides RTC when time is valid)
TempString → temp_string_c[0..n], temp_string_count; water_temp_c if still unset
Sonde      → water_temp_c, water_ph, water_do_mgl, water_conductivity_uscm, water_turbidity_ntu
Weather    → wind_speed_ms, wind_dir_deg (+ air_* only if WX_REG_AIR_* are set)
RainGauge  → rain_mm (accumulated since previous cycle)
```

After the cycle, `main` calls `rtc.syncTo(gnss.lastEpoch())` so the RTC is
disciplined whenever GNSS carries valid time.

`TelemetryRecord` is now **schema v2** — added `temp_string_count` +
`temp_string_c[8]`. The CSV log and header gained `ts_n,ts0..ts7`.

## What must be confirmed against real hardware / datasheets

These are the assumptions baked into `include/config.h` — each is a one-line edit:

1. **RS485 UART routing + DE pin.** `RS485_SERIAL = Serial1` (GP0/GP1) and
   `RS485_DE_PIN = WB_IO2` are guesses. Confirm how the RAK5802 maps onto the
   RAK11310 through the RAK19001 base, and whether direction is auto or DE-pin.
2. **`WB_IO5` / `WB_IO6` for rain gauge and 1-Wire.** Inherited WisBlock IO
   numbers — verify against the RAK19001 schematic, and check the pin can do a
   pin-change interrupt (rain) / has an external 4.7 kΩ pull-up (1-Wire).
3. **Sonde Modbus map.** `SONDE_MODBUS_ID`, `SONDE_REG_FUNCTION` (3 = holding,
   4 = input), and the five `SONDE_REG_*` offsets are placeholders. Fill from the
   sonde's protocol manual. Set an offset to `-1` to skip a parameter. If the
   sonde word-swaps its float32, change `bigEndian` → `littleEndian` in
   `ModbusSonde.cpp`.
4. **Weather station Modbus map.** Same as the sonde (`WX_*`). `WX_REG_AIR_*` are
   `-1` by default so the BME680 stays the met source; set them only if the
   station is authoritative.
5. **`RAIN_MM_PER_TIP`** — 0.2794 mm (0.011″) is typical; calibrate the actual
   gauge by pouring a measured volume.
6. **BME680 address** — `0x76`; some breakouts strap `0x77`.
7. **DS18B20 order** — currently bus-discovery order. To report true
   shallow→deep, record each probe's ROM address and sort `addr_[]` in
   `TempString::begin()`.

## Not yet done (deferred to Phase 3)

- **Data validation** layer (range / rate-of-change / stuck-value checks feeding
  `TELEMETRY_FLAG_SENSOR_FAULT`) — belongs between `sample()` and `append()`.
- **GNSS duty-cycling** — currently powers the module up/down each cycle but a
  cold fix can take minutes; Phase 3 should keep a backup-battery hot-fix or use
  the RTC for time and only fix position occasionally.
- **RS485 / sonde power switching** — the RAK5802 and sonde should be on a
  switched rail (a WisBlock IO pin driving a load switch) and only powered during
  the read window.
