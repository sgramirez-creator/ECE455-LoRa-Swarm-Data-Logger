# ECE455 LoRa Swarm Data Logger

An open-source, low-cost environmental data logging and monitoring system designed for long-term deployment in the field.

The project uses LoRa-based Meshtastic communication to connect distributed sensor nodes and transmit environmental and water-quality measurements to a central server. The collected data will ultimately be displayed geographically on an interactive map.

---

## Project Overview

There is a need for distributed monitoring of water quality and environmental conditions. Commercially available monitoring systems can be extremely expensive, with some systems costing approximately **$110,000 for a single node**.

The goal of this project is to develop a **low-cost, open-source data logger costing less than $700 per node** that can be deployed in the field for extended periods of time.

Each sensor node will collect measurements, record its location and time, and transmit the data through a **915 MHz LoRa Meshtastic mesh network**.

### Primary Goals

- Operate in the field for at least **6–7 months** without requiring a recharge
- Use solar power to potentially enable indefinite operation
- Collect sensor data every **30 minutes**
- Transmit collected data over a **LoRa/Meshtastic mesh network**
- Support multiple external sensor types
- Provide GPS location information
- Include a visible safety light
- Provide a low-cost and easily repairable hardware design
- Use an enclosure that can be made waterproof using inexpensive, readily available materials
- Display sensor data geographically on an interactive map
- Use color/intensity to represent selected sensor measurements
- Maintain an **open-source** design that can be reproduced and repaired easily

---

# System Architecture

```text
                         FIELD SENSOR NODE
┌──────────────────────────────────────────────────────────┐
│                                                          │
│  Water-Quality Sonde ── RS485 ──┐                        │
│  Weather Station ───────────────┤                        │
│  Temperature String ────────────┤                        │
│  BME680 ────────────────────────┤                        │
│  Rain Sensor ───────────────────┤                        │
│                                 ↓                        │
│                         Sensor Management                │
│                                 ↓                        │
│                         Data Processing                  │
│                                 ↓                        │
│                         Telemetry Packet                 │
│                                 ↓                        │ 
│                            Meshtastic                    │ 
│                                 ↓                        │
│                               LoRa                       │
└─────────────────────────────────┼────────────────────────┘
                                  │ 
                            915 MHz LoRa
                                  │ 
                                  ↓
                         ┌──────────────────┐
                         │ Meshtastic Mesh  │
                         │                  │
                         │ Node → Node      │
                         │       → Node     │
                         └────────┬─────────┘
                                  │
                                  ↓
                           Gateway Node
                                  │
                                  ↓
                            Data Server
                                  │
                                  ↓
                              Database
                                  │
                                  ↓
                         Web Dashboard / Map
