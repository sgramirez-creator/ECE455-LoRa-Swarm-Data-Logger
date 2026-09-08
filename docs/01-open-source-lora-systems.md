# Open-Source LoRa Systems — Prior Art & Architecture Research

**Project:** ECE455 LoRa Swarm Data Logger
**Purpose:** Survey existing open-source work for a low-cost (<$700/node), solar-powered,
6–7 month field-deployable environmental/water-quality logger that transmits over a
915 MHz LoRa mesh and renders data on an interactive map.
**Date:** 2026-09-08

---

## 1. Executive summary

There is no single existing project that does everything this one needs. The field
splits into three mature ecosystems that each solve part of the problem:

| Layer | Best existing option | What we reuse | What we still build |
|---|---|---|---|
| **Low-power sensor logger** | Cave Pearl Project, EnviroDIY ModularSensors, Loom | Design patterns, sensor drivers, power-budget methodology | Our board integration + sensor abstraction |
| **LoRa transport / mesh** | Meshtastic (spec-mandated) or LoRaWAN + ChirpStack | Firmware, mesh routing, MQTT bridge, range-test tooling | Custom telemetry payload for non-standard sensors |
| **Server + map** | Meshtastic-Sensor-Network (Node-RED → InfluxDB → Grafana), liamcottle/meshtastic-map | Ingest pipeline, geomap dashboards | Styling data by color/intensity, deployment |

**Key tension to resolve early:** the project spec says "Meshtastic," but Meshtastic is a
chat/telemetry mesh, not a telemetry-at-scale platform. Its built-in Environment
Telemetry only auto-supports a fixed I²C sensor list (BME680/BME280/AHT10/LPS22HB, a few
air-quality parts). A water-quality sonde on RS-485, a temperature string, a tipping-bucket
rain gauge, and a weather station are **not** covered and require either a custom firmware
module or the Serial Module as a bridge. This is the single biggest architecture decision —
see §6.

**Hardware note:** our node uses the **RAK11310 (RP2040 + SX1262)**. For a 6–7 month solar
deployment, the nRF52840-based **RAK4631** is materially more power-efficient (nRF52 idles
in the ~10–20 µA range under Meshtastic; RP2040/ESP32-class parts are much thirstier). RP2040
Meshtastic support is real but has historically lagged (e.g. a LoRa-region-config bug on
RP2040 builds, since closed). If the power budget doesn't close on RP2040, switching the
core module is the cheapest fix — the WisBlock base and sensor modules carry over.

---

## 2. Low-power open-source data loggers (the "logger" half)

### Cave Pearl Project — https://thecavepearlproject.org/
- The reference design for **cheap, long-duration, harsh-environment** Arduino logging.
  ATmega328P + DS3231 RTC + SD (or EEPROM) in PVC-fitting housings.
- Documented **>1 year on 3× AA**, and a coin-cell "lite" variant claimed ~2 years by
  logging to EEPROM instead of SD.
- Peer-reviewed (Sensors 2018, 18(2):530) — good citation for the report's power methodology.
- **What to steal:** the power-optimization playbook (cut the regulator/LEDs on breakouts,
  sleep everything to µA between reads, RTC-driven wake), the PVC waterproofing approach,
  and the "buy 3 breakout boards and wire them" repairability philosophy.
- **Directly relevant:** `Open-Hardware-Leaders/LoRa-Data-Loggers-for-environmental-quality`
  on GitHub is a Cave-Pearl-derived design adapted to **surface water** (turbidity, DO,
  conductivity, pH) — closest single match to our sensor list.

### EnviroDIY — https://www.envirodiy.org/  (Stroud Water Research Center)
- **Mayfly Data Logger**: open-hardware board (ATmega1284P, RTC, µSD, LiPo + solar charge,
  XBee/Bee socket). Purpose-built for solar water-quality stations. Hardware + sketches at
  `EnviroDIY/EnviroDIY_Mayfly_Logger`.
- **ModularSensors** library — `EnviroDIY/ModularSensors`. This is the most valuable
  software artifact in the survey: a **common interface for environmental sensors** on
  Arduino loggers, with built-in sleep-between-readings, and drivers for a large catalog
  of real water-quality sensors (Yosemitech sondes over Modbus/RS-485, Meter Group, Campbell,
  analog turbidity, etc.). Also handles a publish step to Monitor My Watershed / ThingSpeak.
- LoRa is community-supported rather than first-class (forum threads on wiring SX127x
  shields / LoRaWAN Bee modules to the Mayfly).
- **What to steal:** the **sensor abstraction layer design** (Phase 3, first bullet) — don't
  reinvent it; port ModularSensors' `Sensor`/`Variable` pattern, and reuse its RS-485/Modbus
  sonde drivers if our sonde is a supported brand.

### Loom — https://github.com/OPEnSLab-OSU/Loom (Oregon State OPEnS Lab)
- Config-driven ("manager" object + JSON) firmware framework for SAMD/ESP boards that
  bundles sensors, dataloggers, internet, and **radio telemetry (LoRa/nRF)** behind one API.
- Peer-reviewed (HardwareX 2024). Good example of "declare your sensors in config, framework
  handles the loop" — a pattern worth copying for reproducibility.

### FieldKit — https://www.fieldkit.org/  (org: `fieldkit-org` on GitHub)
- Fully open (hardware + firmware + mobile app + web portal) modular environmental logger
  with a backplane/module architecture, GPS, and a hosted data portal with map/graph views.
- Communications are WiFi/cellular, **not LoRa** — but the **modular backplane concept** and
  the **end-to-end "logger → app → web portal with maps"** product design are a strong
  reference for our Phase 4 deliverable.

### Academic LoRa water-quality builds (for the report's literature review)
- *River Water Quality Monitoring Using LoRa-Based IoT* (DOAJ / Preprints 2024) — temp, pH,
  conductivity, turbidity over LoRa, low-cost.
- *Implementation of a Dynamic LoRa Network for Real-Time Monitoring of Water Quality*
  (Designs 2025, 9(4):96).
- *ConnecSenS* (Sensors 2023) — LoRaWAN platform for long-term monitoring of isolated sites;
  good on ruggedization and multi-sensor interfacing.
- *Loom* HardwareX paper (PMC11174476) and *Cave Pearl* Sensors paper (PMC5856100).

---

## 3. LoRa transport options (the "mesh" half)

### 3.1 Meshtastic — https://meshtastic.org  (`meshtastic/firmware`, GPL-3.0)
- **What it is:** application-layer mesh firmware over raw LoRa. Managed flooding with
  hop-limit, no infrastructure required, AES-256 channels, phone/web client, MQTT gateway.
- **RAK11310 support:** yes — official target `firmware-rak11310-*.uf2`, drag-and-drop UF2
  flashing, no BLE/WiFi on RP2040 so setup is via the **web client over USB serial** only.
  RAK sells the "WisMesh RP2040 Starter Kit (RAK11310)" for exactly this.
- **Built-in telemetry:** Telemetry Module → Device / Environment / Air-Quality / Power /
  Health metrics. Sensors on I²C are **auto-detected at boot** from a fixed list
  (BME680/BME280/BMP280/AHT10/LPS22HB/SHT31/INA2xx/MAX17048/…). Environment payload carries
  temperature, RH, barometric pressure (plus a handful more).
- **Position:** GPS module (RAK12500 / u-blox) integrates natively; nodes appear on maps.
- **Getting non-standard sensor data across:**
  1. **Serial Module** (`docs/configuration/module/serial/`) — UART bridge, mode
     TEXTMSG/PROTO/…, up to **237 bytes/packet**, channel must be named `serial`,
     packet framed by a serial gap (default 250 ms). Common pattern: a small companion MCU
     (Pico/Pro Mini) reads the exotic sensors and pushes text/JSON to the Meshtastic node.
  2. **Custom firmware module** — fork the firmware, add a module subclassing the telemetry
     base, extend the protobufs. Most work, cleanest result.
  3. **Detection Sensor Module** — GPIO event → mesh text; too limited for us (booleans only).
- **MQTT / uplink:** gateway node publishes JSON or protobuf to `mqtt.meshtastic.org` (or a
  private broker) under a custom root topic. This is the server-side entry point.
- **Range/loss testing (Phase 4):** built-in **Range Test Module** — sender emits sequential
  numbered packets every 30–60 s; receiver logs them with GPS to CSV for import into Google
  Earth/QGIS. Gives RSSI/SNR/packet-success vs distance directly. Multi-hop is exercised by
  placing relay nodes between endpoints.
- **Power:** super-deep-sleep (SDS) and light-sleep configs exist; nRF52 targets reach
  ~10–20 µA, RP2040/ESP32 far higher. Meshtastic's own guidance: solar nodes disable BT/WiFi/
  serial/screen. Realistic unmodified-Meshtastic battery life on RP2040 is days, not months —
  our 30-min duty cycle + aggressive sleep must be engineered on top.
- **Reference implementation to copy:** **`beegee-tokyo/Meshtastic-Sensor-Network`** (RAK's
  Bernd Giesecke). WisBlock sensor nodes + WiFi gateway, all running Meshtastic (with a
  "bug-fixed" firmware on the gateway so the JSON payload includes PM data), gateway → public
  MQTT → **Node-RED parser → InfluxDB v2 → Grafana** dashboards. This is essentially our
  Phase 4 backend, already built. Noted limitations: incomplete JSON in stock firmware,
  coarse region selection, node IDs arrive as decimals needing a lookup table.

### 3.2 LoRaWAN + ChirpStack — https://www.chirpstack.io  (MIT)
- **What it is:** the standardized star-of-stars stack. Nodes → gateways → ChirpStack network
  server → your app via MQTT/HTTP. Open-source, self-hostable, mature.
- **Why it keeps coming up for environmental telemetry:** built for exactly this shape
  (many sensors, scheduled uplinks, dashboards, backend integration), predictable ADR power
  behavior, huge ecosystem of gateways and decoders, class-A devices sleep hard between
  uplinks.
- **Cost against the <$700 target:** needs at least one gateway in RF range of every node
  (~$100–300 for an outdoor RAK/MikroTik unit, or DIY). If nodes are clustered near one
  gateway with backhaul (cellular/WiFi/Starlink), this is fine and arguably simpler than a
  mesh. If nodes are strung along a river with no single vantage point, mesh wins.
- **Node firmware:** RAK WisBlock-API / RUI3, or Arduino + MCCI LMIC / Beelan-LoRaWAN /
  RadioLib. RAK4631 is a first-class LoRaWAN target; RAK11310 works too.
- **Map/dashboard:** Grafana (Geomap panel) straight off ChirpStack, or push to TagoIO /
  Datacake / ThingsBoard (all have free tiers and native map widgets).

### 3.3 The Things Network / The Things Stack — https://www.thethingsindustries.com
- Hosted LoRaWAN (TTN community network is free). Same node firmware as ChirpStack. Good if
  someone else's gateways already cover the site; less control, fair-use caps on airtime.

### 3.4 Raw LoRa P2P (RadioLib) — https://github.com/jgromes/RadioLib
- Full manual control of the SX1262 from our own Arduino firmware — no mesh, no network
  server. Simplest possible link for a bench bring-up and for a fixed set of nodes talking to
  one collector. **RadioLib is also the cleanest way to drive the SX1262 for a bench LoRa
  smoke-test in Phase 1** regardless of the final transport choice.

### 3.5 MeshCore — https://meshcore.co.uk  (alternative mesh firmware)
- Newer, lighter LoRa mesh with explicit routing (vs Meshtastic's flooding); runs on the
  same RAK WisBlock hardware. Lower overhead, smaller community, fewer integrations. Worth
  knowing as a fallback if Meshtastic's flooding overhead or RP2040 support bites us.

### 3.6 ClusterDuck Protocol — https://clusterduckprotocol.org  (OWL Integrations, MIT)
- LoRa mesh aimed at disaster comms; "DuckLink → MamaDuck → PapaDuck (MQTT)" topology with
  a captive-portal UI. Custom-sensor examples exist (`ClusterDuck-Protocol/Duck-Custom-Examples`).
  Less mature telemetry story than Meshtastic; listed for completeness.

---

## 4. Server, storage & interactive map (the "map" half)

### Meshtastic-native map stacks
- **`liamcottle/meshtastic-map`** (https://meshtastic.liamcottle.net) — Node.js app: subscribe
  to MQTT, store nodes/positions/telemetry, Leaflet map with per-node sidebar (telemetry
  graphs, traceroutes). Self-hostable. Closest turnkey match to "nodes on an interactive map."
- **`komelt/meshtastic-map-mqtt`** (Docker) — packaged variant.
- **meshtastictools.com / MeshMap** — hosted community maps; reference for UI patterns.

### Generic telemetry → map pipelines
- **Node-RED → InfluxDB → Grafana** (the beegee-tokyo stack). Grafana's **Geomap** panel
  colours markers by a measured value → directly delivers "use color/intensity to represent a
  selected measurement."
- **Datacake / TagoIO / ThingsBoard** — low-code IoT platforms with LoRaWAN + Meshtastic
  inputs and drag-and-drop map widgets; fastest path to a demo, free tiers, less
  reproducible/open than self-hosting.
- **Custom web app** — MapLibre GL or Leaflet + a small API over Postgres/InfluxDB/SQLite.
  Most control over the color-by-measurement styling and the "open-source, reproducible"
  requirement; most work.
- **Home Assistant** — Meshtastic MQTT integration exists; useful for a quick internal view,
  not a public map.

### On-node storage (Phase 3 "local data storage")
- µSD via `SdFat` (Bill Greiman) — CSV or line-delimited JSON, mirrors what's sent so data
  survives a mesh outage. RP2040 also has ~2 MB of on-board flash usable as LittleFS for a
  ring buffer if SD is deemed a reliability risk (Cave Pearl's argument for EEPROM logging).

---

## 5. Reusable libraries (Arduino / PlatformIO)

| Need | Library |
|---|---|
| SX1262 raw driver / LoRaWAN / P2P | `jgromes/RadioLib` |
| LoRaWAN (alt) | MCCI `arduino-lmic`, `BeelanLoRaWAN` |
| RAK WisBlock LoRaWAN app framework | `RAKwireless/WisBlock-API-V2`, RUI3 |
| Sensor abstraction (port, don't reinvent) | `EnviroDIY/ModularSensors` |
| BME680 | `boschsensortec/BSEC` or `adafruit/Adafruit_BME680` |
| RTC (DS3231 / RV-3028) | `adafruit/RTClib`, `constiko/RV-3028_C7` |
| GNSS | `mikalhart/TinyGPSPlus`, `sparkfun/SparkFun_u-blox_GNSS_v3` |
| RS-485 / Modbus (sonde) | `4-20ma/modbus-esp8266`, `EnviroDIY/SensorModbusMaster` |
| µSD | `greiman/SdFat` |
| Low power (RP2040) | Pico SDK sleep / `earlephilhower` core `sleep_*`; (nRF52) `Adafruit_nRF52` `waitForEvent` |
| Struct pack/unpack for telemetry | nanopb (protobuf), or a hand-packed `__attribute__((packed))` struct + CRC |

---

## 6. Recommended architecture decision (for discussion)

Three viable node architectures given "one RAK11310 that must both log and transmit":

**Option A — Meshtastic firmware + custom telemetry module.**
Fork `meshtastic/firmware`, add a module that reads our sonde/RS-485/rain/weather sensors and
packs a custom protobuf, keep Meshtastic's mesh + sleep + MQTT + map ecosystem.
- ➕ Mesh routing, range-test tooling, map stacks, MQTT bridge all free. Matches spec.
- ➖ Working inside a large GPL codebase; 237-byte payload ceiling; RP2040 power; protobuf work;
  our 30-min scheduler and deep-sleep budget must be grafted onto Meshtastic's loop.

**Option B — our own Arduino firmware, LoRa via RadioLib (P2P or LoRaWAN/ChirpStack).**
The firmware we've already started owns sensors + SD + RTC scheduler + sleep; SX1262 driven
directly.
- ➕ Full control of power budget (best shot at 6–7 months), payload format, and repairability
  story. ModularSensors ports in cleanly. ChirpStack gives a clean map/dashboard path.
- ➖ No mesh (P2P) unless we add LoRaWAN + a gateway per cluster. Deviates from the "Meshtastic"
  wording in the spec — needs sign-off.

**Option C — two MCUs per node: custom logger + Meshtastic node, bridged by Serial Module.**
- ➕ Each half does what it's good at; minimal firmware forking.
- ➖ More cost, board area, and power per node; two things to keep alive in the field.

**Suggested path:** prototype **Option B for bench bring-up** (RadioLib P2P is the fastest way
to prove the radio in Phase 1), and in parallel evaluate whether the deployment geometry
supports one LoRaWAN gateway per node cluster. If it does → Option B + ChirpStack. If nodes
are truly strung out with no gateway vantage point → Option A (and seriously consider swapping
RAK11310 → RAK4631 for the power budget). Either way, **port ModularSensors' sensor
abstraction** and **reuse the Node-RED → InfluxDB → Grafana-Geomap** backend from
beegee-tokyo/Meshtastic-Sensor-Network.

---

## 7. Direct hits to clone / read first

1. `beegee-tokyo/Meshtastic-Sensor-Network` — end-to-end WisBlock + Meshtastic + MQTT + Node-RED + InfluxDB + Grafana. **Read this first.**
2. `EnviroDIY/ModularSensors` — sensor abstraction layer + real sonde drivers.
3. `Open-Hardware-Leaders/LoRa-Data-Loggers-for-environmental-quality` — Cave-Pearl-derived surface-water logger.
4. Cave Pearl Project site + Sensors 2018 paper — power budget & waterproofing methodology.
5. `liamcottle/meshtastic-map` — turnkey node map.
6. `meshtastic/firmware` Telemetry + Serial + Range Test modules — the integration surface.
7. ChirpStack docs — the LoRaWAN alternative to weigh against Meshtastic.

---

## Sources

- https://meshtastic.org/docs/hardware/devices/rak-wireless/wisblock/core-module/
- https://docs.rakwireless.com/product-categories/meshtastic/wismesh-starter-kit-rak11310/quickstart/
- https://meshtastic.org/docs/getting-started/flashing-firmware/nrf52/
- https://github.com/meshtastic/firmware/issues/5359
- https://meshtastic.org/docs/configuration/module/telemetry/
- https://meshtastic.org/docs/configuration/module/serial/
- https://meshtastic.org/docs/configuration/module/range-test/
- https://meshtastic.org/docs/software/integrations/mqtt/
- https://deepwiki.com/meshtastic/firmware/9.1-telemetry-and-sensor-modules
- https://deepwiki.com/meshtastic/meshtastic/3.4-module-system
- https://github.com/beegee-tokyo/Meshtastic-Sensor-Network
- https://github.com/liamcottle/meshtastic-map
- https://meshtastic.liamcottle.net/
- https://thecavepearlproject.org/
- https://pmc.ncbi.nlm.nih.gov/articles/PMC5856100/  (Cave Pearl, Sensors 2018)
- https://www.hackster.io/news/cave-pearl-s-arduino-powered-data-logger-cuts-a-few-components-for-a-year-long-battery-life-92c374a9d046
- https://github.com/Open-Hardware-Leaders/LoRa-Data-Loggers-for-environmental-quality
- https://www.envirodiy.org/mayfly/
- https://github.com/EnviroDIY/EnviroDIY_Mayfly_Logger
- https://github.com/EnviroDIY/ModularSensors
- https://www.envirodiy.org/topic/mayfly-with-lora-module-physical-connection/
- https://www.ncbi.nlm.nih.gov/pmc/articles/PMC11174476/  (Loom, HardwareX 2024)
- https://github.com/OPEnSLab-OSU/Loom
- https://www.fieldkit.org/platform/
- https://www.chirpstack.io
- https://www.seeedstudio.com/blog/2026/06/11/lorawan-vs-meshtastic-choosing-the-right-network-for-your-project/
- https://dev.to/vlad_avramut/lora-vs-lorawan-vs-meshtastic-the-architecture-most-people-get-wrong-288f
- https://openelab.io/blogs/learn/lorawan-vs-lora-vs-meshtastic-vs-meshcore-vs-lora-p2p
- https://github.com/jgromes/RadioLib
- https://nodakmesh.org/blog/rak-wismesh-1w-vs-rak4631-comparison
- https://nodakmesh.org/blog/meshtastic-range-test-guide
- https://github.com/ddubick/Meshtastic-esp32/blob/master/docs/software/power.md
- https://www.preprints.org/manuscript/202410.2025  (River Water Quality Monitoring Using LoRa-Based IoT)
- https://doi.org/10.3390/designs9040096  (Dynamic LoRa Network for Water Quality)
- https://www.ncbi.nlm.nih.gov/pmc/articles/PMC10059706/  (ConnecSenS, Sensors 2023)
- https://github.com/NerfyGek0/MeshtasticIOAddon
- https://core-electronics.com.au/videos/how-to-send-custom-sensor-data-meshtastic-for-makers-workshop
