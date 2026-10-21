"""Store Swarm node data from a USB-connected Meshtastic radio (the base station) into sensor_data.db for app.py.

    python receiver.py                          # auto-detect the Meshtastic radio on USB
    python receiver.py --port /dev/cu.usbmodem1101
    python receiver.py --stdin                  # type/paste sonde JSON by hand, no radio needed
"""

import argparse
import json
import signal
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

# Per-packet radio metadata, stored with both sonde readings and node status rows.
LINK_COLUMNS = {
    "packet_id": "INTEGER",
    "received_at": "TEXT",
    "rx_snr": "REAL",
    "rx_rssi": "INTEGER",
    "hops": "INTEGER",
    "relay_node": "INTEGER",
    "gateway_id": "INTEGER",
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
    **LINK_COLUMNS,
}

db_lock = threading.Lock()


def add_missing_columns(conn, table, columns):
    existing = {row[1] for row in conn.execute(f"PRAGMA table_info({table})")}
    for column, sql_type in columns.items():
        if column not in existing:
            conn.execute(f"ALTER TABLE {table} ADD COLUMN {column} {sql_type}")


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
    add_missing_columns(conn, "measurements", MEASUREMENT_EXTRA_COLUMNS)
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
    add_missing_columns(conn, "node_status", LINK_COLUMNS)
    conn.execute("""
    CREATE TABLE IF NOT EXISTS weather (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        timestamp TEXT,
        node_id INTEGER,
        seq INTEGER,
        simulated INTEGER,
        wind_speed REAL,
        wind_dir REAL,
        air_temp REAL,
        humidity REAL,
        pressure REAL,
        lux REAL,
        rain REAL,
        latitude REAL,
        longitude REAL
    )
    """)
    add_missing_columns(conn, "weather", LINK_COLUMNS)
    conn.execute("CREATE UNIQUE INDEX IF NOT EXISTS weather_packet ON weather(packet_id)")
    conn.execute("""
    CREATE TABLE IF NOT EXISTS nodes (
        node_id INTEGER PRIMARY KEY,
        node_hex TEXT,
        long_name TEXT,
        short_name TEXT,
        hw_model TEXT,
        role TEXT,
        last_heard INTEGER,
        updated_at TEXT
    )
    """)
    conn.commit()
    return conn


def local_time(epoch=None):
    moment = datetime.fromtimestamp(epoch) if epoch else datetime.now()
    return moment.strftime("%Y-%m-%d %H:%M:%S")


def node_hex(node_id):
    return f"!{int(node_id):08x}"


def as_float(value):
    if value is None:
        return None
    number = float(value)
    return number if number == number else None


def link_fields(packet, gateway_id):
    if not packet:
        return {"received_at": local_time()}
    hop_start, hop_limit = packet.get("hopStart"), packet.get("hopLimit", 0)
    return {
        "packet_id": packet.get("id"),
        "received_at": local_time(),
        "rx_snr": packet.get("rxSnr"),
        "rx_rssi": packet.get("rxRssi"),
        "hops": hop_start - hop_limit if hop_start else None,
        "relay_node": packet.get("relayNode"),
        "gateway_id": gateway_id,
    }


def insert_row(conn, table, row):
    columns = ", ".join(row)
    placeholders = ", ".join("?" for _ in row)
    with db_lock:
        cursor = conn.execute(
            f"INSERT OR IGNORE INTO {table} ({columns}) VALUES ({placeholders})",
            tuple(row.values()),
        )
        conn.commit()
    return cursor.rowcount


def upsert_node(conn, node_id, user=None, last_heard=None):
    user = user or {}
    with db_lock:
        conn.execute(
            """
            INSERT INTO nodes (node_id, node_hex, long_name, short_name, hw_model, role, last_heard, updated_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(node_id) DO UPDATE SET
                long_name = COALESCE(excluded.long_name, nodes.long_name),
                short_name = COALESCE(excluded.short_name, nodes.short_name),
                hw_model = COALESCE(excluded.hw_model, nodes.hw_model),
                role = COALESCE(excluded.role, nodes.role),
                last_heard = COALESCE(excluded.last_heard, nodes.last_heard),
                updated_at = excluded.updated_at
            """,
            (
                int(node_id),
                node_hex(node_id),
                user.get("longName"),
                user.get("shortName"),
                user.get("hwModel"),
                user.get("role"),
                last_heard,
                local_time(),
            ),
        )
        conn.commit()


def measured_time(data):
    device_time = data.get("t")
    if isinstance(device_time, (int, float)) and device_time >= MIN_VALID_EPOCH:
        return int(device_time)
    return None


def store_weather(conn, data, packet=None, gateway_id=None):
    node_id = int(data["node_id"])
    row = {
        "timestamp": local_time(measured_time(data)),
        "node_id": node_id,
        "seq": data.get("seq"),
        "simulated": 1 if data.get("sim") else 0,
        "wind_speed": as_float(data.get("wind_speed")),
        "wind_dir": as_float(data.get("wind_dir")),
        "air_temp": as_float(data.get("air_temp")),
        "humidity": as_float(data.get("humidity")),
        "pressure": as_float(data.get("pressure")),
        "lux": as_float(data.get("lux")),
        "rain": as_float(data.get("rain")),
        "latitude": as_float(data.get("lat")),
        "longitude": as_float(data.get("lon")),
        **link_fields(packet, gateway_id),
    }
    if insert_row(conn, "weather", row):
        print(f"[{row['timestamp']}] {node_hex(node_id)} weather seq {row['seq']}: {data}")


def store_measurement(conn, data, packet=None, gateway_id=None):
    node_id = int(data["node_id"])
    measured = measured_time(data)

    row = {
        "timestamp": local_time(measured),
        "node_id": node_id,
        "seq": data.get("seq"),
        "simulated": 1 if data.get("sim") else 0,
        "sonde_status": data.get("status"),
        "error": data.get("err"),
        **link_fields(packet, gateway_id),
    }
    for key, column in SONDE_FIELDS.items():
        row[column] = as_float(data.get(key))

    if insert_row(conn, "measurements", row):
        label = "SIMULATED " if row["simulated"] else ""
        signal = f" rssi={row.get('rx_rssi')} snr={row.get('rx_snr')} hops={row.get('hops')}" if packet else ""
        print(f"[{row['timestamp']}] {label}{node_hex(node_id)} seq {row['seq']}{signal}: {data}")


def store_node_status(conn, packet, gateway_id):
    decoded = packet.get("decoded", {})
    telemetry = decoded.get("telemetry", {})
    device = telemetry.get("deviceMetrics", {})
    environment = telemetry.get("environmentMetrics", {})
    position = decoded.get("position", {})
    if not (device or environment or position.get("latitude") is not None):
        return

    insert_row(conn, "node_status", {
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
        **link_fields(packet, gateway_id),
    })


def on_receive(packet, interface, conn, gateway_id):
    decoded = packet.get("decoded", {})
    portnum = decoded.get("portnum")
    try:
        sender = packet.get("from")
        if sender is not None:
            known = interface.nodes.get(packet.get("fromId"), {}) if interface and interface.nodes else {}
            user = decoded.get("user") if portnum == "NODEINFO_APP" else known.get("user")
            upsert_node(conn, sender, user, known.get("lastHeard"))

        if portnum == "PRIVATE_APP":
            data = json.loads(bytes(decoded.get("payload", b"")).decode("utf-8"))
            # Relayed packets keep their original sender, so a reading must come from the node it names.
            if "node_id" in data and int(data["node_id"]) == sender:
                if data.get("type") == "wx":
                    store_weather(conn, data, packet, gateway_id)
                else:
                    store_measurement(conn, data, packet, gateway_id)
            elif "node_id" in data:
                print(f"Ignored reading claiming node {data['node_id']} sent by {packet.get('fromId')}")
        elif portnum in ("TELEMETRY_APP", "POSITION_APP"):
            store_node_status(conn, packet, gateway_id)
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
            data = json.loads(line)
            (store_weather if data.get("type") == "wx" else store_measurement)(conn, data)
        except (json.JSONDecodeError, KeyError, ValueError, TypeError) as e:
            print("Invalid message:", e)


def run_radio(conn, port):
    try:
        from pubsub import pub
        from meshtastic.serial_interface import SerialInterface
    except ImportError:
        sys.exit("The meshtastic package is missing: pip install -r requirements.txt")

    gateway = {"id": None}

    # pypubsub keeps only weak references, so the handler must stay referenced while we run.
    def handle(packet, interface):
        on_receive(packet, interface, conn, gateway["id"])

    pub.subscribe(handle, "meshtastic.receive")
    interface = SerialInterface(devPath=port)
    me = interface.getMyNodeInfo() or {}
    gateway["id"] = me.get("num")
    for node in (interface.nodes or {}).values():
        if "num" in node:
            upsert_node(conn, node["num"], node.get("user"), node.get("lastHeard"))

    user = me.get("user", {})
    print(f"Base station: {user.get('longName', 'Meshtastic radio')} ({user.get('id', '?')}). Waiting for data...")
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

    # Background jobs ignore Ctrl-C, so dashboard.sh stops us with SIGTERM; exit through the cleanup path.
    signal.signal(signal.SIGTERM, lambda signum, frame: sys.exit(0))

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
