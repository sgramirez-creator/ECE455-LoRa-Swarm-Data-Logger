
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


def load_data():
    with sqlite3.connect(DB_FILE) as conn:
        df = pd.read_sql_query(
            """
            SELECT timestamp, node_id, water_temp,
                   ph, conductivity, dissolved_oxygen,
                   turbidity
            FROM measurements
            ORDER BY timestamp
            """,
            conn
        )

    df["timestamp"] = pd.to_datetime(df["timestamp"])
    return df


@st.fragment(run_every="5s")
def live_dashboard():

    df = load_data()

    if df.empty:
        st.warning("No sensor data received yet.")
        return

    st.sidebar.header("Controls")

    node_options = ["All"] + sorted(
        df["node_id"].unique().tolist()
    )

    selected_node = st.sidebar.selectbox(
        "Select Node",
        node_options
    )

    measurement_options = {
        "Water Temperature": "water_temp",
        "pH": "ph",
        "Conductivity": "conductivity",
        "Dissolved Oxygen": "dissolved_oxygen",
        "Turbidity": "turbidity"
    }

    selected_measurement = st.sidebar.selectbox(
        "Trend Measurement",
        list(measurement_options.keys())
    )

    measurement_column = measurement_options[
        selected_measurement
    ]

    if selected_node == "All":
        filtered_df = df.copy()
    else:
        filtered_df = df[
            df["node_id"] == selected_node
        ].copy()

    latest = filtered_df.iloc[-1]

    st.subheader("Latest Reading")

    col1, col2, col3, col4, col5 = st.columns(5)

    col1.metric(
        "Water Temperature",
        f"{latest['water_temp']:.2f} °C"
    )

    col2.metric(
        "pH",
        f"{latest['ph']:.2f}"
    )

    col3.metric(
        "Conductivity",
        f"{latest['conductivity']:.1f} µS/cm"
    )

    col4.metric(
        "Dissolved Oxygen",
        f"{latest['dissolved_oxygen']:.2f} mg/L"
    )

    col5.metric(
        "Turbidity",
        f"{latest['turbidity']:.2f} NTU"
    )

    st.caption(
        "Last update: "
        + latest["timestamp"].strftime(
            "%m/%d/%Y %I:%M:%S %p"
        )
    )

    st.subheader(f"{selected_measurement} Trend")

    chart_df = filtered_df[
        ["timestamp", measurement_column]
    ].set_index("timestamp")

    st.line_chart(chart_df)

    st.subheader("Sensor Data")

    display_df = filtered_df.rename(
        columns={
            "timestamp": "Timestamp",
            "node_id": "Node ID",
            "water_temp": "Water Temp (C)",
            "ph": "pH",
            "conductivity": "Conductivity (uS/cm)",
            "dissolved_oxygen": "DO (mg/L)",
            "turbidity": "Turbidity (NTU)"
        }
    ).copy()

    display_df["Timestamp"] = (
        display_df["Timestamp"]
        .dt.strftime("%m/%d/%Y %I:%M:%S %p")
    )

    st.dataframe(
        display_df,
        use_container_width=True,
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


live_dashboard()
