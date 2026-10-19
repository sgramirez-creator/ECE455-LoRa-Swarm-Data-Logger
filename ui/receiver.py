
import sqlite3
import json
from datetime import datetime

DB_FILE = "sensor_data.db"

conn = sqlite3.connect(DB_FILE)
cursor = conn.cursor()

cursor.execute("""
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

conn.commit()

print("Receiver started...")
print("Enter a simulated Meshtastic message.")
print("Type 'exit' to quit.")

while True:
    message = input("\nIncoming message: ")

    if message.lower() == "exit":
        break

    try:
        data = json.loads(message)

        timestamp = datetime.now().strftime(
            "%Y-%m-%d %H:%M:%S"
        )

        node_id = int(data["node_id"])
        water_temp = float(data["water_temp"])
        ph = float(data["ph"])
        conductivity = float(data["conductivity"])
        dissolved_oxygen = float(data["dissolved_oxygen"])
        turbidity = float(data["turbidity"])

        cursor.execute("""
        INSERT INTO measurements (
            timestamp,
            node_id,
            water_temp,
            ph,
            conductivity,
            dissolved_oxygen,
            turbidity
        )
        VALUES (?, ?, ?, ?, ?, ?, ?)
        """, (
            timestamp,
            node_id,
            water_temp,
            ph,
            conductivity,
            dissolved_oxygen,
            turbidity
        ))

        conn.commit()

        print("Measurement saved successfully!")
        print(data)

    except (json.JSONDecodeError, KeyError,
            ValueError, TypeError) as e:
        print("Invalid message:", e)

conn.close()
