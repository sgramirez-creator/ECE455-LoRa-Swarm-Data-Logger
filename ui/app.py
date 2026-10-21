
import html
import math
import sqlite3
from datetime import datetime, timedelta
from io import BytesIO
from pathlib import Path

import altair as alt
import numpy as np
import pandas as pd
import pydeck as pdk
import streamlit as st

APP_TITLE = "Lake Swarm Monitor"
APP_SUBTITLE = "Water quality and weather from the LoRa mesh"

st.set_page_config(page_title=APP_TITLE, layout="wide")

st.markdown(
    """
    <style>
    .block-container {padding-top: 3.6rem; padding-bottom: 1rem; max-width: 1700px;}
    .stAppDeployButton {display: none;}
    .app-title {font-size: 1.8rem; font-weight: 700; line-height: 1.25; margin: 0;}
    .app-subtitle {opacity: .65; margin: .15rem 0 .8rem;}
    .section {font-size: 1.02rem; font-weight: 600; margin: .6rem 0 .3rem;}
    .kpi-grid {display: grid; grid-template-columns: repeat(auto-fill, minmax(155px, 1fr)); gap: .55rem; margin-bottom: .6rem;}
    .kpi {border: 1px solid rgba(128,128,128,.25); border-radius: .6rem; padding: .5rem .75rem; background: rgba(128,128,128,.06); min-width: 0;}
    .kpi-label {font-size: .76rem; opacity: .7; white-space: nowrap; overflow: hidden; text-overflow: ellipsis;}
    .kpi-value {font-size: 1.3rem; font-weight: 600; line-height: 1.55; white-space: nowrap; overflow: hidden; text-overflow: ellipsis;}
    .kpi-sub {font-size: .7rem; opacity: .6; white-space: nowrap; overflow: hidden; text-overflow: ellipsis;}
    .dot {display: inline-block; width: .55rem; height: .55rem; border-radius: 50%; margin-right: .4rem;}
    </style>
    """,
    unsafe_allow_html=True,
)

DB_FILE = Path(__file__).resolve().parent / "sensor_data.db"

# column -> (label, unit, decimals)
WATER = {
    "water_temp": ("Water Temp", "°C", 2),
    "ph": ("pH", "", 2),
    "conductivity": ("Conductivity", "µS/cm", 1),
    "dissolved_oxygen": ("Dissolved O₂", "mg/L", 2),
    "turbidity": ("Turbidity", "NTU", 2),
    "orp": ("ORP", "mV", 1),
    "chlorophyll": ("Chlorophyll", "µg/L", 2),
    "bga": ("Blue-Green Algae", "cells/mL", 0),
}
WEATHER = {
    "wind_speed": ("Wind Speed", "m/s", 1),
    "wind_dir": ("Wind Direction", "°", 0),
    "air_temp": ("Air Temp", "°C", 1),
    "humidity": ("Humidity", "%", 1),
    "pressure": ("Pressure", "hPa", 0),
    "lux": ("Light", "lux", 0),
    "rain": ("Rain (accumulated)", "mm", 1),
}
# Physically plausible ranges; readings outside them are treated as missing.
WEATHER_RANGES = {
    "wind_speed": (0, 75),
    "wind_dir": (0, 360),
    "air_temp": (-50, 70),
    "humidity": (0, 100),
    "pressure": (300, 1100),
    "lux": (0, 250_000),
    "rain": (0, 1_000),
}
# Values that can be mapped across the lake: key -> (table, column).
MAP_METRICS = {f"water:{c}": ("water", c) for c in WATER} | {
    f"weather:{c}": ("weather", c) for c in WEATHER if c != "wind_dir"
}
WINDOWS = {"Last 15 min": 15, "Last hour": 60, "Last 6 hours": 360, "Last 24 hours": 1440, "All time": None}
VIEWS = ["Overview", "Water", "Weather", "Network", "Data"]
ONLINE_WINDOW = timedelta(minutes=5)
COMPASS = ["N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"]
# Heat-map ramp: blue (lowest) -> cyan -> yellow -> red (highest).
RAMP = np.array([[49, 54, 149], [69, 170, 220], [254, 224, 100], [215, 48, 39]])
LINK_COLUMNS = ["rx_rssi", "rx_snr", "hops", "gateway_id", "seq", "received_at", "latitude", "longitude", "simulated"]


# ---------------------------------------------------------------- data


def node_hex(node_id):
    return f"!{int(node_id):08x}"


def load_table(name):
    if not DB_FILE.exists():
        return pd.DataFrame()
    with sqlite3.connect(DB_FILE) as conn:
        exists = conn.execute("SELECT 1 FROM sqlite_master WHERE type='table' AND name=?", (name,)).fetchone()
        if not exists:
            return pd.DataFrame()
        df = pd.read_sql_query(f"SELECT * FROM {name}", conn)
    if "timestamp" in df.columns:
        df["timestamp"] = pd.to_datetime(df["timestamp"])
        df = df.sort_values(["timestamp", "id"] if "id" in df.columns else "timestamp", ignore_index=True)
    return df


def with_columns(df, columns):
    df = df.copy()
    for column in columns:
        if column not in df.columns:
            df[column] = np.nan
    return df


def load_names():
    names = {}
    for _, row in load_table("nodes").iterrows():
        if pd.notna(row.get("long_name")) and row["long_name"]:
            names[int(row["node_id"])] = (row["long_name"], row.get("hw_model"))
    return names


def load_data(include_simulated):
    water = with_columns(load_table("measurements"), ["node_id", "timestamp", "error", *WATER, *LINK_COLUMNS])
    weather = with_columns(load_table("weather"), ["node_id", "timestamp", *WEATHER, *LINK_COLUMNS])
    status = with_columns(
        load_table("node_status"),
        ["node_id", "timestamp", "battery_level", "voltage", "air_temp", "humidity", "pressure", "latitude", "longitude"],
    )

    # Each weather value is its own Modbus read, so an impossible value only invalidates that field.
    bad = pd.Series(False, index=weather.index)
    for column, (low, high) in WEATHER_RANGES.items():
        out_of_range = weather[column].notna() & ~weather[column].between(low, high)
        weather.loc[out_of_range, column] = np.nan
        bad |= out_of_range
    # Only recent frames matter for the warning; old bad frames shouldn't keep it on forever.
    invalid = int(bad.iloc[-10:].sum())

    if not include_simulated:
        water = water[water["simulated"].fillna(0) == 0]
        weather = weather[weather["simulated"].fillna(0) == 0]
    return water, weather, status, invalid


def in_window(df, minutes):
    if minutes is None or df.empty:
        return df
    return df[df["timestamp"] >= datetime.now() - timedelta(minutes=minutes)]


def for_node(df, node):
    return df if node == "All" or df.empty else df[df["node_id"] == node]


# ---------------------------------------------------------------- formatting


def display_name(node_id, names):
    if pd.isna(node_id):
        return "—"
    return names.get(int(node_id), (None, None))[0] or f"Node {node_hex(node_id)[-4:]}"


def node_label(node_id, names):
    return f"{display_name(node_id, names)} ({node_hex(node_id)})"


def received_by(row, names):
    if pd.isna(row.get("gateway_id")):
        return "—"
    if int(row["gateway_id"]) == int(row["node_id"]):
        return "USB (direct)"
    return display_name(row["gateway_id"], names)


def fmt(value, unit="", decimals=1):
    if value is None or pd.isna(value):
        return "—"
    return f"{value:,.{decimals}f} {unit}".strip()


def fmt_wind_dir(value):
    if pd.isna(value):
        return "—"
    return f"{value:.0f}° {COMPASS[int((value % 360) / 22.5 + 0.5) % 16]}"


def time_ago(moment):
    if moment is None or pd.isna(moment):
        return "never"
    seconds = max((datetime.now() - moment).total_seconds(), 0)
    for limit, size, unit in ((60, 1, "s"), (3600, 60, "min"), (86400, 3600, "h")):
        if seconds < limit:
            return f"{int(seconds // size)} {unit} ago"
    return f"{int(seconds // 86400)} d ago"


def clock(moment):
    return "—" if moment is None or pd.isna(moment) else moment.strftime("%m/%d %I:%M:%S %p")


def ramp_color(t):
    t = min(max(t, 0.0), 1.0) * (len(RAMP) - 1)
    i = min(int(t), len(RAMP) - 2)
    return [int(c) for c in RAMP[i] + (RAMP[i + 1] - RAMP[i]) * (t - i)]


def section(title):
    st.markdown(f'<div class="section">{html.escape(title)}</div>', unsafe_allow_html=True)


def cards(items):
    # Node names come from the mesh, so everything is escaped before going into HTML.
    parts = []
    for label, value, sub in items:
        parts.append(
            f'<div class="kpi" title="{html.escape(f"{label}: {value}")}">'
            f'<div class="kpi-label">{html.escape(label)}</div>'
            f'<div class="kpi-value">{html.escape(value)}</div>'
            f'<div class="kpi-sub">{html.escape(sub or "")}&nbsp;</div></div>'
        )
    st.markdown(f'<div class="kpi-grid">{"".join(parts)}</div>', unsafe_allow_html=True)


def line_chart(df, column, title, names, height=220):
    data = df.dropna(subset=[column])
    if data.empty:
        st.caption(f"No {title.lower()} data in this time range.")
        return
    data = data.assign(node=[display_name(n, names) for n in data["node_id"]])[["timestamp", "node", column]]
    span = data["timestamp"].max() - data["timestamp"].min()
    axis_format = "%H:%M:%S" if span < timedelta(minutes=10) else "%H:%M" if span < timedelta(days=1) else "%b %d %H:%M"
    chart = (
        alt.Chart(data)
        .mark_line(strokeWidth=2, interpolate="monotone")
        .encode(
            x=alt.X("timestamp:T", title=None, axis=alt.Axis(format=axis_format, labelAngle=0, tickCount=6, grid=False)),
            y=alt.Y(f"{column}:Q", title=title, scale=alt.Scale(zero=False)),
            color=alt.Color("node:N", title=None, legend=alt.Legend(orient="top") if data["node"].nunique() > 1 else None),
            tooltip=[alt.Tooltip("timestamp:T", title="Time", format="%I:%M:%S %p"), "node:N", alt.Tooltip(f"{column}:Q", title=title)],
        )
        .properties(height=height, width="container")
    )
    st.altair_chart(chart, use_container_width=True)


def delivery_ratio(seq):
    # Sequence numbers restart at 0 when a node reboots; only score the current run.
    values = seq.dropna().astype(int).tolist()
    if len(values) < 2:
        return None
    start = max(i for i in range(len(values)) if i == 0 or values[i] < values[i - 1])
    run = values[start:]
    expected = run[-1] - run[0] + 1
    return 100.0 * len(set(run)) / expected if expected > 0 else None


# ---------------------------------------------------------------- heat map


def idw_grid(lat, lon, values, cells=40, power=2.0):
    lat0, lon0 = lat.mean(), lon.mean()
    m_lat = 111_320.0
    m_lon = 111_320.0 * math.cos(math.radians(lat0))
    x, y = (lon - lon0) * m_lon, (lat - lat0) * m_lat

    pad = max(0.3 * max(np.ptp(x), np.ptp(y)), 75.0)
    side = max(np.ptp(x), np.ptp(y)) + 2 * pad
    step = side / cells
    cx, cy = (x.min() + x.max()) / 2, (y.min() + y.max()) / 2
    axis = (np.arange(cells) + 0.5) * step - side / 2
    gx, gy = np.meshgrid(cx + axis, cy + axis)

    dist = np.hypot(gx[..., None] - x, gy[..., None] - y)
    weights = 1.0 / np.maximum(dist, 1.0) ** power
    grid = (weights * values).sum(axis=-1) / weights.sum(axis=-1)
    return pd.DataFrame({"lat": gy.ravel() / m_lat + lat0, "lon": gx.ravel() / m_lon + lon0, "value": grid.ravel()}), step, side


def show_heatmap(source, status, names, column, label, unit, decimals, height=400):
    columns = ["node_id", "latitude", "longitude"]
    # Prefer the position sent with the newest reading; fall back to plain GNSS position packets.
    positions = source.dropna(subset=["latitude", "longitude"])[columns].drop_duplicates("node_id", keep="last")
    if not status.empty:
        fixes = status.dropna(subset=["latitude", "longitude"])[columns]
        fixes = fixes[fixes["node_id"].isin(source["node_id"]) & ~fixes["node_id"].isin(positions["node_id"])]
        if not fixes.empty:
            positions = pd.concat([positions, fixes.drop_duplicates("node_id", keep="last")], ignore_index=True)
    if positions.empty:
        st.info(
            "No GNSS positions yet. The map fills in once nodes get a fix (the RAK12500 needs a view of the sky) "
            "or have a fixed position set in the Meshtastic app."
        )
        return

    positions = positions.copy()
    readings = source.dropna(subset=[column])
    positions["value"] = positions["node_id"].map(readings.groupby("node_id")[column].last())
    radio = source.dropna(subset=["rx_rssi"]).groupby("node_id")[["rx_rssi", "rx_snr"]].last()
    positions["name"] = [display_name(n, names) for n in positions["node_id"]]
    positions["node_hex"] = [node_hex(n) for n in positions["node_id"]]
    positions["value_text"] = [fmt(v, unit, decimals) for v in positions["value"]]
    positions["signal"] = [
        f"RSSI {radio.loc[n, 'rx_rssi']:.0f} dBm · SNR {radio.loc[n, 'rx_snr']:.1f} dB" if n in radio.index else "USB"
        for n in positions["node_id"]
    ]

    valued = positions.dropna(subset=["value"])
    low, high = (valued["value"].min(), valued["value"].max()) if not valued.empty else (0.0, 0.0)

    def scale(v):
        return 0.5 if high == low else (v - low) / (high - low)

    positions["color"] = [ramp_color(scale(v)) + [255] if pd.notna(v) else [128, 128, 128, 255] for v in positions["value"]]
    layers, side = [], 300.0
    if not valued.empty:
        grid, step, side = idw_grid(
            valued["latitude"].to_numpy(float), valued["longitude"].to_numpy(float), valued["value"].to_numpy(float)
        )
        grid["color"] = [ramp_color(scale(v)) + [110] for v in grid["value"]]
        layers.append(pdk.Layer(
            "ScatterplotLayer", grid, get_position=["lon", "lat"], get_fill_color="color",
            get_radius=step * 0.72, radius_units="meters", stroked=False, pickable=False,
        ))
    layers.append(pdk.Layer(
        "ScatterplotLayer", positions, get_position=["longitude", "latitude"], get_fill_color="color",
        get_line_color=[255, 255, 255], line_width_min_pixels=2, stroked=True,
        get_radius=5, radius_units="meters", radius_min_pixels=8, pickable=True,
    ))
    layers.append(pdk.Layer(
        "TextLayer", positions, get_position=["longitude", "latitude"], get_text="name",
        get_size=13, get_color=[255, 255, 255], get_pixel_offset=[0, -20], pickable=False,
    ))

    lat0 = positions["latitude"].mean()
    zoom = math.log2(156_543 * math.cos(math.radians(lat0)) * 700 / max(side, 50.0))
    view = pdk.ViewState(latitude=lat0, longitude=positions["longitude"].mean(), zoom=min(max(zoom, 3), 19))
    tooltip = {"html": "<b>{name}</b> ({node_hex})<br/>" + html.escape(label) + ": {value_text}<br/>{signal}"}
    st.pydeck_chart(pdk.Deck(layers=layers, initial_view_state=view, tooltip=tooltip, map_style=None), height=height)
    if not valued.empty:
        st.caption(
            f"{label}: blue = {fmt(low, unit, decimals)}, red = {fmt(high, unit, decimals)} across {len(valued)} node(s). "
            "Values between nodes are interpolated."
        )


# ---------------------------------------------------------------- views


def last_seen(water, weather, status):
    frames = [f[["node_id", "timestamp"]] for f in (water, weather, status) if not f.empty]
    if not frames:
        return pd.Series(dtype="datetime64[ns]")
    return pd.concat(frames).groupby("node_id")["timestamp"].max()


def status_strip(water, weather, status, names, node):
    seen = last_seen(for_node(water, node), for_node(weather, node), for_node(status, node))
    seen = seen[seen.index.isin(set(water["node_id"]) | set(weather["node_id"]))]
    online = int((datetime.now() - seen < ONLINE_WINDOW).sum()) if not seen.empty else 0
    newest = seen.max() if not seen.empty else None

    radio = pd.concat([f for f in (for_node(water, node), for_node(weather, node)) if not f.empty] or [pd.DataFrame()])
    link_value, link_sub, gateway_value, gateway_sub = "—", "", "—", ""
    if not radio.empty:
        radio = radio.sort_values("timestamp")
        latest = radio.iloc[-1]
        gateway_value = received_by(latest, names)
        gateway_sub = node_hex(latest["gateway_id"]) if pd.notna(latest["gateway_id"]) else ""
        over_air = radio.dropna(subset=["rx_rssi"])
        if not over_air.empty:
            last = over_air.iloc[-1]
            link_value = f"{last['rx_rssi']:.0f} dBm"
            link_sub = f"SNR {last['rx_snr']:.1f} dB · {int(last['hops']) if pd.notna(last['hops']) else '?'} hop(s)"

    cards([
        ("Nodes online", f"{online} / {len(seen)}", "heard in the last 5 min"),
        ("Last data", time_ago(newest), clock(newest)),
        ("Received by", gateway_value, gateway_sub),
        ("Latest link", link_value, link_sub),
    ])


def latest_row(df, columns):
    rows = df.dropna(subset=columns, how="all")
    return rows.iloc[-1] if not rows.empty else None


def water_cards(water, names):
    row = latest_row(water, list(WATER))
    if row is None:
        st.caption("No sonde readings yet.")
        return
    sub = f"{display_name(row['node_id'], names)} · {clock(row['timestamp'])}"
    cards([(label, fmt(row[c], unit, dec), sub) for c, (label, unit, dec) in WATER.items()])


def weather_cards(weather, names, invalid):
    if invalid:
        st.warning(f"{invalid} of the last 10 weather readings had out-of-range values and are hidden.")
    row = latest_row(weather, [c for c in WEATHER])
    if row is None:
        if not invalid:
            st.caption("No weather station readings yet.")
        return
    sub = f"{display_name(row['node_id'], names)} · {clock(row['timestamp'])}"
    cards([
        (label, fmt_wind_dir(row[c]) if c == "wind_dir" else fmt(row[c], unit, dec), sub)
        for c, (label, unit, dec) in WEATHER.items()
    ])


def sonde_alerts(water, names):
    for node_id, rows in water.groupby("node_id"):
        last = rows.iloc[-1]
        if pd.notna(last["error"]):
            st.warning(f"Sonde on {display_name(node_id, names)} is not responding (last attempt {clock(last['timestamp'])}).")


def network_table(water, weather, status, names):
    gateways = set(water["gateway_id"].dropna().astype(int)) | set(weather["gateway_id"].dropna().astype(int))
    seen = last_seen(water, weather, status)
    rows = []
    for node_id in sorted(set(water["node_id"]) | set(weather["node_id"]) | gateways):
        group = water[water["node_id"] == node_id]
        radio = pd.concat([group, weather[weather["node_id"] == node_id]]).dropna(subset=["rx_rssi"]).sort_values("timestamp")
        node_status = status[status["node_id"] == node_id]

        def last_status(column):
            series = node_status[column].dropna()
            return series.iloc[-1] if not series.empty else None

        battery = last_status("battery_level")
        sensor = node_id in set(water["node_id"]) | set(weather["node_id"])
        role = "Sensor + base" if sensor and node_id in gateways else "Sensor" if sensor else "Base station"
        heard = seen.get(node_id)
        rows.append({
            "Status": "Online" if heard is not None and datetime.now() - heard < ONLINE_WINDOW else "Offline",
            "Node": display_name(node_id, names),
            "Node ID": node_hex(node_id),
            "Hardware": names.get(int(node_id), (None, None))[1] or "—",
            "Role": role,
            "Last Heard": time_ago(heard) if heard is not None else "—",
            "Delivery %": delivery_ratio(group["seq"]) if not group.empty else None,
            "RSSI dBm": radio["rx_rssi"].iloc[-1] if not radio.empty else None,
            "SNR dB": radio["rx_snr"].iloc[-1] if not radio.empty else None,
            "Hops": radio["hops"].iloc[-1] if not radio.empty else None,
            "Battery": "External" if battery is not None and battery > 100 else fmt(battery, "%", 0),
            "Voltage": fmt(last_status("voltage"), "V", 2),
            "BME680 Temp": fmt(last_status("air_temp"), "°C", 1),
        })
    table = pd.DataFrame(rows)
    if table.empty:
        st.caption("No nodes heard yet.")
        return
    table["Hops"] = table["Hops"].astype("Int64")
    st.dataframe(
        table, width="stretch", hide_index=True,
        column_config={
            "Delivery %": st.column_config.ProgressColumn(min_value=0, max_value=100, format="%.0f%%"),
            "RSSI dBm": st.column_config.NumberColumn(format="%.0f"),
            "SNR dB": st.column_config.NumberColumn(format="%.1f"),
        },
    )


def export_tables(water, weather, names):
    def tidy(df, columns, labels):
        if df.empty:
            return pd.DataFrame()
        out = df.assign(
            node=[display_name(n, names) for n in df["node_id"]],
            node_hex=[node_hex(n) for n in df["node_id"]],
            received_by=[received_by(row, names) for _, row in df.iterrows()],
        ).iloc[::-1]
        keep = ["timestamp", "node", "node_hex", *columns, "latitude", "longitude", "rx_rssi", "rx_snr", "hops", "received_by"]
        out = out[[c for c in keep if c in out.columns and out[c].notna().any()]]
        out = out.rename(columns={
            "timestamp": "Timestamp", "node": "Node", "node_hex": "Node ID", "latitude": "Latitude",
            "longitude": "Longitude", "rx_rssi": "RSSI (dBm)", "rx_snr": "SNR (dB)", "hops": "Hops",
            "received_by": "Received By", **labels,
        })
        out["Timestamp"] = out["Timestamp"].dt.strftime("%m/%d/%Y %I:%M:%S %p")
        return out

    water_table = tidy(water, [*WATER, "sonde_status", "error"], {
        **{c: f"{label} ({unit})" if unit else label for c, (label, unit, _) in WATER.items()},
        "sonde_status": "Sonde Status", "error": "Error",
    })
    weather_table = tidy(weather, list(WEATHER), {c: f"{label} ({unit})" for c, (label, unit, _) in WEATHER.items()})
    return water_table, weather_table


def excel_bytes(water_table, weather_table):
    output = BytesIO()
    with pd.ExcelWriter(output, engine="openpyxl") as writer:
        (water_table if not water_table.empty else pd.DataFrame({"Info": ["No water data"]})).to_excel(
            writer, index=False, sheet_name="Water Quality"
        )
        if not weather_table.empty:
            weather_table.to_excel(writer, index=False, sheet_name="Weather")
    return output.getvalue()


# ---------------------------------------------------------------- page


def control_choices(water, weather, names):
    nodes = tuple(sorted(set(water["node_id"].dropna().astype(int)) | set(weather["node_id"].dropna().astype(int))))
    labels = tuple(node_label(n, names) for n in nodes)
    tables = {"water": water, "weather": weather}
    metrics = tuple(key for key, (table, column) in MAP_METRICS.items() if tables[table][column].notna().any())
    return nodes, labels, metrics or ("water:water_temp",)


def metric_spec(key):
    table, column = MAP_METRICS[key]
    label, unit, decimals = (WATER if table == "water" else WEATHER)[column]
    return table, column, label, unit, decimals


st.markdown(f'<p class="app-title">{APP_TITLE}</p><p class="app-subtitle">{APP_SUBTITLE}</p>', unsafe_allow_html=True)

# Streamlit forbids st.sidebar inside a fragment, so the controls live outside it.
initial_names = load_names()
initial_water, initial_weather, _, _ = load_data(include_simulated=True)
choices = control_choices(initial_water, initial_weather, initial_names)
label_for = dict(zip(choices[0], choices[1]))

view = st.segmented_control("View", VIEWS, default="Overview", label_visibility="collapsed", key="view") or "Overview"

with st.sidebar:
    st.header("Controls")
    selected_node = st.selectbox(
        "Node", ["All", *choices[0]], format_func=lambda n: "All nodes" if n == "All" else label_for.get(n, str(n))
    )
    selected_metric = st.selectbox(
        "Map & trend measurement", choices[2],
        format_func=lambda k: ("Water · " if k.startswith("water") else "Weather · ") + metric_spec(k)[2],
    )
    window_label = st.selectbox("Time range", list(WINDOWS), index=1)
    with st.expander("Options"):
        include_simulated = st.checkbox("Include simulated readings", value=True)


@st.fragment(run_every="5s")
def live_view(view, node, metric_key, window_label, include_simulated, choices):
    names = load_names()
    water, weather, status, invalid = load_data(include_simulated)
    if water.empty and weather.empty:
        st.info("No data received yet. Start the receiver with `./dashboard.sh start` and power a sensor node.")
        return
    # Rebuild the sidebar when a new node, node name, or measurement type shows up.
    if control_choices(*load_data(True)[:2], names) != choices:
        st.rerun()

    minutes = WINDOWS[window_label]
    water_now, weather_now = for_node(water, node), for_node(weather, node)
    water_win, weather_win = in_window(water_now, minutes), in_window(weather_now, minutes)
    table, column, label, unit, decimals = metric_spec(metric_key)

    if view == "Overview":
        status_strip(water, weather, status, names, node)
        sonde_alerts(water_now, names)
        left, right = st.columns(2, gap="medium")
        with left:
            section("Water quality")
            water_cards(water_now, names)
        with right:
            section("Weather")
            weather_cards(weather_now, names, invalid)
        map_col, trend_col = st.columns([3, 2], gap="medium")
        source = water_now if table == "water" else weather_now
        with map_col:
            section(f"Lake map · {label}")
            show_heatmap(source, status, names, column, label, unit, decimals, height=380)
        with trend_col:
            section(f"{label} · {window_label.lower()}")
            line_chart(water_win if table == "water" else weather_win, column, f"{label} ({unit})" if unit else label, names, height=380)

    elif view == "Water":
        sonde_alerts(water_now, names)
        water_cards(water_now, names)
        available = [c for c in WATER if water_win[c].notna().any()]
        if not available:
            st.caption("No sonde readings in this time range.")
        for i in range(0, len(available), 2):
            cols = st.columns(2, gap="medium")
            for col, c in zip(cols, available[i:i + 2]):
                with col:
                    name, unit_c, _ = WATER[c]
                    section(name)
                    line_chart(water_win, c, f"{name} ({unit_c})" if unit_c else name, names)

    elif view == "Weather":
        weather_cards(weather_now, names, invalid)
        available = [c for c in WEATHER if weather_win[c].notna().any()]
        if not available and not invalid:
            st.caption("No weather readings in this time range.")
        for i in range(0, len(available), 2):
            cols = st.columns(2, gap="medium")
            for col, c in zip(cols, available[i:i + 2]):
                with col:
                    name, unit_c, _ = WEATHER[c]
                    section(name)
                    line_chart(weather_win, c, f"{name} ({unit_c})", names)

    elif view == "Network":
        status_strip(water, weather, status, names, node)
        network_table(water_now, weather_now, status, names)
        radio = pd.concat([water_win, weather_win]).dropna(subset=["rx_rssi"]).sort_values("timestamp")
        if radio.empty:
            st.caption("No over-the-air packets in this time range (USB readings carry no signal strength).")
        else:
            rssi_col, snr_col = st.columns(2, gap="medium")
            with rssi_col:
                section("RSSI")
                line_chart(radio, "rx_rssi", "RSSI (dBm)", names)
            with snr_col:
                section("SNR")
                line_chart(radio, "rx_snr", "SNR (dB)", names)
            st.caption("Signal strength is measured by the receiving base station on the last radio hop.")

    elif view == "Data":
        water_table, weather_table = export_tables(water_win, weather_win, names)
        water_tab, weather_tab = st.tabs([f"Water quality ({len(water_table)})", f"Weather ({len(weather_table)})"])
        with water_tab:
            st.dataframe(water_table, width="stretch", hide_index=True, height=460)
        with weather_tab:
            st.dataframe(weather_table, width="stretch", hide_index=True, height=460)
        st.download_button(
            "Export to Excel",
            data=excel_bytes(water_table, weather_table),
            file_name=f"lake_swarm_{datetime.now():%Y%m%d_%H%M}.xlsx",
            mime="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
        )
        st.caption(f"Showing {window_label.lower()}. Change the time range in the sidebar to export more.")


live_view(view, selected_node, selected_metric, window_label, include_simulated, choices)
