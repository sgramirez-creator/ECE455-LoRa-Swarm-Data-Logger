import sqlite3
import random
import time
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
print("Writing simulated sensor data to SQLite.")

while True:

    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    node_id = random.choice([1, 2])

    water_temp = round(random.uniform(18.0, 20.0), 2)
    ph = round(random.uniform(7.0, 7.5), 2)
    conductivity = round(random.uniform(400, 420), 1)
    dissolved_oxygen = round(random.uniform(7.5, 8.5), 2)
    turbidity = round(random.uniform(2.0, 2.5), 2)

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

    print(
        timestamp,
        node_id,
        water_temp,
        ph,
        conductivity,
        dissolved_oxygen,
        turbidity
    )

    time.sleep(10)