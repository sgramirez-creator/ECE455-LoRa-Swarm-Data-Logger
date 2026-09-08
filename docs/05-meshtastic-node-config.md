# Meshtastic Node Configuration (companion-bridge architecture)

Each field node is **two boards**:

```
  ┌─────────────────────────┐          ┌──────────────────────────────┐
  │  Datalogger MCU          │  UART    │  RAK11310  (stock Meshtastic) │
  │  (this firmware)         │─────────▶│  Serial Module, TEXTMSG mode  │
  │  sensors, RTC, SD, sleep │  MESH_   │  SX1262 → 915 MHz mesh         │
  └─────────────────────────┘  UART     └──────────────────────────────┘
        wakes every 30 min,                  always-on relay + gateway path
        sends one line, sleeps
```

The datalogger owns all sensors and power management. The RAK11310 only does
mesh radio + the serial ingest. They share nothing but the UART (and ground).

## 1. Wiring

| Datalogger (RP2040 companion) | → | RAK11310 (Meshtastic) |
|---|---|---|
| `MESH_UART` TX = GP4 (UART1 TX) | → | Serial Module RX pin |
| `MESH_UART` RX = GP5 (UART1 RX) | ← | Serial Module TX pin |
| GND | — | GND |

Pick the RAK11310 Serial Module pins from a free WisBlock IO/UART header and set
them in the Meshtastic config below. Common choice on the RAK19001 IO slot:
`RXD2`/`TXD2`. Keep the link at 3.3 V logic (both are RP2040 — fine).

> If you later move the datalogger onto a cheaper board (XIAO RP2040,
> RP2040-Zero), only `MESH_UART` and the pin map in `config.h` change.

## 2. Meshtastic firmware

Flash **stock** `firmware-rak11310-*.uf2` (drag-and-drop UF2, no fork).
Minimum version 2.5.x; newer is better for RP2040 fixes.

Configure via the Web Client over USB (RP2040 has no BLE/Wi-Fi):

### LoRa
- Region: `US` (915 MHz)
- Modem preset: `LONG_FAST` (default) or `LONG_SLOW` for range over throughput
- Channel: a private primary channel with a shared PSK across the swarm
- `Hop limit`: 3–5 depending on network diameter

### Serial Module  (`Config → Module → Serial`)
| Setting | Value |
|---|---|
| `enabled` | true |
| `mode` | `TEXTMSG` |
| `baud` | `38400` (must equal `MESH_UART_BAUD`) |
| `rxd` / `txd` | the GPIOs you wired above |
| `timeout` | `250` ms (must be **<** `MESH_UART_QUIET_MS` = 300) |
| `override_console_serial_port` | false |

Create a channel named exactly **`serial`** with **uplink enabled** — the Serial
Module only forwards to the mesh on that channel.

### Power  (`Config → Power`)
- Sensor/relay nodes on solar: leave the radio **always on** (a sleeping node
  won't relay for its neighbours). `is_power_saving` only if the node is a leaf.
- If mains/solar is solid, this node can also be the **router** for its area.

### Telemetry Module
- **Disable** the built-in Environment Telemetry — our datalogger is the sensor
  source and the BME680 is wired to *it*, not to the RAK11310's I²C. Leaving it
  on just adds duplicate/empty packets.
- Device Metrics (battery/voltage/air-util) can stay on — useful for the map.

### Position
- If a GPS (RAK12500) is on the **RAK11310**, enable Position and it appears on
  the map natively. If GPS is on the **datalogger** instead, position rides
  inside our JSON payload and the map is driven from MQTT (below).

## 3. Payload format on the wire

The datalogger emits one text line per cycle:

```
SWARM {"v":3,"id":1,"sq":42,"up":1234,"t":1757337000,"fl":0,"at":21.5,"ah":55,"ap":1013.2,"wt":12.3,"ph":7.2,"do":9.1,"bv":3.85}
```

Short keys (`telemetry_to_json`): `v` schema, `id` node, `sq` sequence,
`t` epoch, `fl` flags, `at/ah/ap` air, `wt/ph/do/ec/tb` water, `rn/ws/wd`
rain+wind, `ts` temp-string array, `bv/pv` battery/solar, `lat/lon/alt/sv` GNSS.
NaN fields are omitted.

If the JSON would exceed ~228 bytes it falls back to:

```
SWARM64 <base64 of the 117-byte packed TelemetryRecord>
```

## 4. Server side (reuse existing open source)

Follow **`beegee-tokyo/Meshtastic-Sensor-Network`**:

1. A gateway node (any always-on Meshtastic node with Wi-Fi/Ethernet, e.g. a
   RAK11200 or a Pi) with **MQTT enabled**, JSON output on, custom root topic.
2. **Node-RED** subscribes, matches the `SWARM ` / `SWARM64 ` prefix, parses the
   JSON (or base64-decodes the struct per `telemetry.h` schema `v`), maps the
   decimal node id → name.
3. **InfluxDB v2** stores the points.
4. **Grafana** + Geomap panel renders nodes on the map, colour = selected
   measurement.

`sq` (sequence) + `id` let the server compute per-node packet-loss for Phase 4.
