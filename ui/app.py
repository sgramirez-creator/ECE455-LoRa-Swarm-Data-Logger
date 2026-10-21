
import streamlit as st
import pandas as pd
import sqlite3
from io import BytesIO
from pathlib import Path

st.set_page_config(
    page_title="Water Quality Dashboard",
    layout="wide"
)

st.title("Water Quality Monitoring Dashboard")

DB_FILE = Path(__file__).resolve().parent / "sensor_data.db"

# column -> (label, unit, decimals)
MEASUREMENTS = {
    "water_temp": ("Water Temperature", "°C", 2),
    "ph": ("pH", "", 2),
    "conductivity": ("Conductivity", "µS/cm", 1),
    "dissolved_oxygen": ("Dissolved Oxygen", "mg/L", 2),
    "turbidity": ("Turbidity", "NTU", 2),
    "orp": ("ORP", "mV", 1),
    "chlorophyll": ("Chlorophyll", "µg/L", 2),
    "bga": ("Blue-Green Algae", "cells/mL", 0),
}

TABLE_LABELS = {
    "timestamp": "Timestamp",
    "node_id": "Node ID",
    "water_temp": "Water Temp (C)",
    "ph": "pH",
    "conductivity": "Conductivity (uS/cm)",
    "dissolved_oxygen": "DO (mg/L)",
    "turbidity": "Turbidity (NTU)",
    "orp": "ORP (mV)",
    "chlorophyll": "Chlorophyll (ug/L)",
    "bga": "BGA (cells/mL)",
    "latitude": "Latitude",
    "longitude": "Longitude",
    "sonde_status": "Sonde Status",
    "error": "Error",
    "simulated": "Simulated",
    "rx_snr": "SNR (dB)",
    "rx_rssi": "RSSI (dBm)",
}


def load_table(name):
    if not DB_FILE.exists():
        return pd.DataFrame()
    with sqlite3.connect(DB_FILE) as conn:
        exists = conn.execute(
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?", (name,)
        ).fetchone()
        if not exists:
            return pd.DataFrame()
        df = pd.read_sql_query(f"SELECT * FROM {name} ORDER BY timestamp", conn)
    df["timestamp"] = pd.to_datetime(df["timestamp"])
    return df


def load_data():
    df = load_table("measurements")
    for column in list(MEASUREMENTS) + ["latitude", "longitude", "simulated", "error"]:
        if column not in df.columns:
            df[column] = None
    return df


def format_value(value, unit, decimals):
    if pd.isna(value):
        return "—"
    return f"{value:,.{decimals}f} {unit}".strip()


def value_color(value, low, high):
    if pd.isna(value):
        return "#808080"
    t = 0.5 if high == low else min(max((value - low) / (high - low), 0.0), 1.0)
    return "#{:02x}{:02x}{:02x}".format(int(40 + 215 * t), 80, int(255 - 215 * t))


def show_map(filtered_df, node_status, measurement_column, measurement_label):
    columns = ["node_id", "latitude", "longitude"]
    # Prefer the position attached to the newest sonde message; fall back to plain GNSS position packets.
    latest = filtered_df.dropna(subset=["latitude", "longitude"])[columns].drop_duplicates("node_id", keep="last")
    if not node_status.empty:
        fixes = node_status.dropna(subset=["latitude", "longitude"])[columns]
        fixes = fixes[fixes["node_id"].isin(filtered_df["node_id"]) & ~fixes["node_id"].isin(latest["node_id"])]
        latest = pd.concat([latest, fixes.drop_duplicates("node_id", keep="last")], ignore_index=True)

    if latest.empty:
        st.info("No GNSS positions received yet.")
        return

    latest = latest.copy()
    readings = filtered_df.dropna(subset=[measurement_column])
    latest_value = readings.groupby("node_id")[measurement_column].last()
    latest["value"] = latest["node_id"].map(latest_value)

    low, high = (readings[measurement_column].min(), readings[measurement_column].max()) if not readings.empty else (0, 0)
    latest["color"] = [value_color(v, low, high) for v in latest["value"]]
    st.map(latest, latitude="latitude", longitude="longitude", color="color", size=40)
    st.caption(f"Color: {measurement_label} (blue = lowest, red = highest of the selected data).")


def show_node_status(node_status, selected_node):
    if node_status.empty:
        return
    if selected_node != "All":
        node_status = node_status[node_status["node_id"] == selected_node]
    if node_status.empty:
        return

    st.subheader("Node Status")
    fields = {
        "air_temp": ("Air Temp", "°C", 1),
        "humidity": ("Humidity", "%", 0),
        "pressure": ("Pressure", "hPa", 1),
        "battery_level": ("Battery", "%", 0),
        "voltage": ("Voltage", "V", 2),
    }
    for node_id, rows in node_status.groupby("node_id"):
        if rows[list(fields)].isna().all().all():
            continue
        cols = st.columns(len(fields) + 1)
        cols[0].metric("Node", str(node_id))
        for col, (column, (label, unit, decimals)) in zip(cols[1:], fields.items()):
            series = rows[column].dropna() if column in rows else pd.Series(dtype=float)
            col.metric(label, format_value(series.iloc[-1] if not series.empty else None, unit, decimals))


def control_choices(df):
    nodes = tuple(sorted(df["node_id"].unique().tolist())) if not df.empty else ()
    available = {
        label: column
        for column, (label, _, _) in MEASUREMENTS.items()
        if not df.empty and df[column].notna().any()
    } or {"Water Temperature": "water_temp"}
    return nodes, available


# Streamlit forbids st.sidebar inside a fragment, so the controls live outside it.
known_nodes, available = control_choices(load_data())

st.sidebar.header("Controls")

selected_node = st.sidebar.selectbox(
    "Select Node",
    ["All"] + list(known_nodes)
)

selected_measurement = st.sidebar.selectbox(
    "Trend Measurement",
    list(available.keys())
)

include_simulated = st.sidebar.checkbox(
    "Include simulated readings",
    value=True
)


@st.fragment(run_every="5s")
def live_dashboard(selected_node, selected_measurement, include_simulated, known_nodes, available):

    df = load_data()
    node_status = load_table("node_status")

    if df.empty:
        st.warning("No sensor data received yet.")
        return

    # Rebuild the sidebar when a new node or measurement type shows up.
    if control_choices(df) != (known_nodes, available):
        st.rerun()

    measurement_column = available[
        selected_measurement
    ]

    if selected_node == "All":
        filtered_df = df.copy()
    else:
        filtered_df = df[
            df["node_id"] == selected_node
        ].copy()

    if not include_simulated:
        filtered_df = filtered_df[filtered_df["simulated"].fillna(0) == 0]

    if filtered_df.empty:
        st.warning("No readings match the current filters.")
        return

    readings = filtered_df.dropna(subset=list(MEASUREMENTS), how="all")
    last_attempt = filtered_df.iloc[-1]

    if pd.notna(last_attempt["error"]):
        st.error(
            f"Sonde on node {last_attempt['node_id']} did not respond at "
            + last_attempt["timestamp"].strftime("%m/%d/%Y %I:%M:%S %p")
            + f" ({last_attempt['error']})."
        )

    if readings.empty:
        st.warning("No sonde readings yet; only status messages have been received.")
    else:
        latest = readings.iloc[-1]

        st.subheader("Latest Reading")

        main_columns = list(MEASUREMENTS)[:5]
        extra_columns = [c for c in list(MEASUREMENTS)[5:] if readings[c].notna().any()]
        for row_columns in (main_columns, extra_columns):
            if not row_columns:
                continue
            cols = st.columns(5)
            for col, column in zip(cols, row_columns):
                label, unit, decimals = MEASUREMENTS[column]
                col.metric(label, format_value(latest[column], unit, decimals))

        caption = "Last update: " + latest["timestamp"].strftime("%m/%d/%Y %I:%M:%S %p")
        if latest["simulated"] == 1:
            caption += " (simulated)"
        st.caption(caption)

        st.subheader(f"{selected_measurement} Trend")

        chart_df = readings[
            ["timestamp", measurement_column]
        ].set_index("timestamp")

        st.line_chart(chart_df)

    st.subheader("Node Locations")
    show_map(filtered_df, node_status, measurement_column, selected_measurement)

    show_node_status(node_status, selected_node)

    st.subheader("Sensor Data")

    display_columns = [c for c in TABLE_LABELS if c in filtered_df.columns and filtered_df[c].notna().any()]
    display_df = filtered_df[display_columns].rename(
        columns=TABLE_LABELS
    ).copy()

    display_df["Timestamp"] = (
        display_df["Timestamp"]
        .dt.strftime("%m/%d/%Y %I:%M:%S %p")
    )

    st.dataframe(
        display_df,
        width="stretch",
        hide_index=True
    )

    output = BytesIO()

    with pd.ExcelWriter(
        output,
        engine="openpyxl"
    ) as writer:
        display_df.to_excel(
            writer,
            index=False,
            sheet_name="Sensor Data"
        )

    st.download_button(
        label="Export Filtered Data to Excel",
        data=output.getvalue(),
        file_name="water_quality_data.xlsx",
        mime=(
            "application/vnd.openxmlformats-officedocument."
            "spreadsheetml.sheet"
        )
    )


live_dashboard(selected_node, selected_measurement, include_simulated, known_nodes, available)
