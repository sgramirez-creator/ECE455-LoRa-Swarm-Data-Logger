"""Store Swarm node data from a USB-connected Meshtastic radio into sensor_data.db for app.py.

    python receiver.py                          # auto-detect the Meshtastic radio on USB
    python receiver.py --port /dev/cu.usbmodem1101
    python receiver.py --stdin                  # type/paste sonde JSON by hand, no radio needed
"""

import argparse
import json
import sqlite3
import sys
import threading
import time
from datetime import datetime
from pathlib import Path

DB_FILE = Path(__file__).resolve().parent / "sensor_data.db"

# Device clock values before this are treated as unset (2024-01-01 UTC).
MIN_VALID_EPOCH = 1704067200

# JSON key sent by the firmware (SondeModule.cpp) -> measurements column.
SONDE_FIELDS = {
    "water_temp": "water_temp",
    "ph": "ph",
    "conductivity": "conductivity",
    "dissolved_oxygen": "dissolved_oxygen",
    "turbidity": "turbidity",
    "orp": "orp",
    "chl": "chlorophyll",
    "bga": "bga",
    "lat": "latitude",
    "lon": "longitude",
}

MEASUREMENT_EXTRA_COLUMNS = {
    "orp": "REAL",
    "chlorophyll": "REAL",
    "bga": "REAL",
    "latitude": "REAL",
    "longitude": "REAL",
    "sonde_status": "INTEGER",
    "error": "TEXT",
    "seq": "INTEGER",
    "simulated": "INTEGER",
    "packet_id": "INTEGER",
    "received_at": "TEXT",
    "rx_snr": "REAL",
    "rx_rssi": "INTEGER",
}

db_lock = threading.Lock()


def open_db():
    conn = sqlite3.connect(DB_FILE, check_same_thread=False)
    conn.execute("""
    CREATE TABLE IF NOT EXISTS measurements (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        timestamp TEXT,
        node_id INTEGER,
        water_temp REAL,
        ph REAL,
        conductivity REAL,
        dissolved_oxygen REAL,
        turbidity REAL
    )
    """)
    existing = {row[1] for row in conn.execute("PRAGMA table_info(measurements)")}
    for column, sql_type in MEASUREMENT_EXTRA_COLUMNS.items():
        if column not in existing:
            conn.execute(f"ALTER TABLE measurements ADD COLUMN {column} {sql_type}")
    conn.execute(
        "CREATE UNIQUE INDEX IF NOT EXISTS measurements_packet ON measurements(packet_id)"
    )
    conn.execute("""
    CREATE TABLE IF NOT EXISTS node_status (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        timestamp TEXT,
        node_id INTEGER,
        battery_level REAL,
        voltage REAL,
        air_temp REAL,
        humidity REAL,
        pressure REAL,
        gas_resistance REAL,
        latitude REAL,
        longitude REAL,
        altitude REAL,
        rx_snr REAL,
        rx_rssi INTEGER,
        packet_id INTEGER UNIQUE
    )
    """)
    conn.commit()
    return conn


def local_time(epoch=None):
    moment = datetime.fromtimestamp(epoch) if epoch else datetime.now()
    return moment.strftime("%Y-%m-%d %H:%M:%S")


def as_float(value):
    if value is None:
        return None
    number = float(value)
    return number if number == number else None


def store_measurement(conn, data, packet=None):
    node_id = int(data["node_id"])
    device_time = data.get("t")
    measured = (
        int(device_time)
        if isinstance(device_time, (int, float)) and device_time >= MIN_VALID_EPOCH
        else None
    )

    row = {
        "timestamp": local_time(measured),
        "node_id": node_id,
        "seq": data.get("seq"),
        "simulated": 1 if data.get("sim") else 0,
        "sonde_status": data.get("status"),
        "error": data.get("err"),
        "received_at": local_time(),
    }
    for key, column in SONDE_FIELDS.items():
        row[column] = as_float(data.get(key))
    if packet:
        row["packet_id"] = packet.get("id")
        row["rx_snr"] = packet.get("rxSnr")
        row["rx_rssi"] = packet.get("rxRssi")

    columns = ", ".join(row)
    placeholders = ", ".join("?" for _ in row)
    with db_lock:
        cursor = conn.execute(
            f"INSERT OR IGNORE INTO measurements ({columns}) VALUES ({placeholders})",
            tuple(row.values()),
        )
        conn.commit()
    if cursor.rowcount:
        label = "SIMULATED " if row["simulated"] else ""
        print(f"[{row['timestamp']}] {label}node {node_id} seq {row['seq']}: {data}")


def store_node_status(conn, packet):
    decoded = packet.get("decoded", {})
    telemetry = decoded.get("telemetry", {})
    device = telemetry.get("deviceMetrics", {})
    environment = telemetry.get("environmentMetrics", {})
    position = decoded.get("position", {})
    if not (device or environment or position.get("latitude") is not None):
        return

    row = {
        "timestamp": local_time(),
        "node_id": packet.get("from"),
        "battery_level": as_float(device.get("batteryLevel")),
        "voltage": as_float(device.get("voltage")),
        "air_temp": as_float(environment.get("temperature")),
        "humidity": as_float(environment.get("relativeHumidity")),
        "pressure": as_float(environment.get("barometricPressure")),
        "gas_resistance": as_float(environment.get("gasResistance")),
        "latitude": as_float(position.get("latitude")),
        "longitude": as_float(position.get("longitude")),
        "altitude": as_float(position.get("altitude")),
        "rx_snr": packet.get("rxSnr"),
        "rx_rssi": packet.get("rxRssi"),
        "packet_id": packet.get("id"),
    }
    columns = ", ".join(row)
    placeholders = ", ".join("?" for _ in row)
    with db_lock:
        conn.execute(
            f"INSERT OR IGNORE INTO node_status ({columns}) VALUES ({placeholders})",
            tuple(row.values()),
        )
        conn.commit()


def on_receive(packet, interface, conn):
    decoded = packet.get("decoded", {})
    portnum = decoded.get("portnum")
    try:
        if portnum == "PRIVATE_APP":
            data = json.loads(bytes(decoded.get("payload", b"")).decode("utf-8"))
            if "node_id" in data:
                store_measurement(conn, data, packet)
        elif portnum in ("TELEMETRY_APP", "POSITION_APP"):
            store_node_status(conn, packet)
    except (json.JSONDecodeError, UnicodeDecodeError, KeyError, ValueError, TypeError) as e:
        print(f"Ignored {portnum} packet from {packet.get('fromId')}: {e}")


def run_stdin(conn):
    print("Paste a sonde JSON message per line ('exit' to quit).")
    for line in sys.stdin:
        line = line.strip()
        if line.lower() == "exit":
            break
        if not line:
            continue
        try:
            store_measurement(conn, json.loads(line))
        except (json.JSONDecodeError, KeyError, ValueError, TypeError) as e:
            print("Invalid message:", e)


def run_radio(conn, port):
    try:
        from pubsub import pub
        from meshtastic.serial_interface import SerialInterface
    except ImportError:
        sys.exit("The meshtastic package is missing: pip install -r requirements.txt")

    pub.subscribe(lambda packet, interface: on_receive(packet, interface, conn), "meshtastic.receive")
    interface = SerialInterface(devPath=port)
    node = interface.getMyNodeInfo() or {}
    user = node.get("user", {})
    print(f"Connected to {user.get('longName', 'Meshtastic radio')} ({user.get('id', '?')}). Waiting for data...")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass
    finally:
        interface.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", help="serial port of the Meshtastic radio (default: auto-detect)")
    parser.add_argument("--stdin", action="store_true", help="read JSON messages from the keyboard instead")
    args = parser.parse_args()

    conn = open_db()
    print(f"Writing to {DB_FILE}")
    try:
        if args.stdin:
            run_stdin(conn)
        else:
            run_radio(conn, args.port)
    finally:
        conn.close()


if __name__ == "__main__":
    main()
