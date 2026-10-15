"""
DHVANI · Ground Station Dashboard

Loads the trained ML model (dhvani_model.joblib) and runs live inference on
incoming STM32 telemetry (via USB serial) or on a replayed log file.

Usage:
    streamlit run app.py                              # replay sample/log_demo.txt
    streamlit run app.py -- COM9                       # live serial on COM9 @ 115200
    streamlit run app.py -- replay:sample/log_demo.txt # explicit replay mode

IMPORTANT: The ML model was trained on SYNTHETIC development data
(see ml/generate_dataset.py). This dashboard performs real inference with a
real trained model — but the model's accuracy claims apply to synthetic
data only, not validated real-world sensor accuracy.
"""

import collections
import sys
import threading
import time
from collections import Counter
from pathlib import Path

import altair as alt
import joblib
import numpy as np
import pandas as pd
import streamlit as st

from telemetry import parse_telemetry_line

# ----------------------------------------------------------------------------
# Constants
# ----------------------------------------------------------------------------

MODEL_PATH = "dhvani_model.joblib"
FEATURES = ["temp_c", "tds_ppm", "clarity"]

LABEL_DISPLAY = {
    "clear_fresh": "Clear freshwater",
    "clear_reef": "Clear shallow reef",
    "muddy_river": "Muddy river",
    "muddy_estuary": "Muddy estuary",
}

CLASS_ACCENT = {
    "clear_fresh": "#38BDF8",
    "clear_reef": "#2DD4BF",
    "muddy_river": "#D97706",
    "muddy_estuary": "#B45309",
}

POWER_MODE_COLOR = {
    "NORMAL": "#22C55E",
    "ECONOMY": "#F59E0B",
    "SURVIVE": "#EF4444",
}

BUFFER_MAXLEN = 600
DEFAULT_BAUD = 115200
REPLAY_PING_INTERVAL_S = 1.0
UI_REFRESH_INTERVAL_S = 1.0
VOTE_WINDOW = 5
GRAPH_WINDOW_SECONDS = 60
DEFAULT_REPLAY_TARGET = "replay:sample/log_demo.txt"

CLARITY_COLOR = "#38BDF8"
FREQ_COLOR = "#F59E0B"


# ----------------------------------------------------------------------------
# Shared telemetry buffer + background reader threads
# ----------------------------------------------------------------------------


class TelemetryBuffer:
    """Thread-safe rolling buffer of parsed telemetry records.

    Written to by a background reader thread, read by the Streamlit render
    loop. Decoupled so the UI redraw never blocks on serial/file I/O.
    """

    def __init__(self, maxlen=BUFFER_MAXLEN):
        self._deque = collections.deque(maxlen=maxlen)
        self._lock = threading.Lock()
        self.status = {"connected": False, "message": "starting...", "source": None}

    def append(self, record):
        record = dict(record)
        record["t_wall"] = time.time()
        with self._lock:
            self._deque.append(record)

    def snapshot(self):
        with self._lock:
            return list(self._deque)


def _read_replay_lines(filepath):
    with open(filepath, "r") as f:
        return [
            line.strip()
            for line in f
            if line.strip() and not line.strip().startswith("#")
        ]


def serial_reader_loop(buffer, port, baud=DEFAULT_BAUD):
    """Continuously reads from the STM32 serial port, reconnecting on failure."""
    import serial  # imported lazily so replay mode never requires pyserial's backend

    buffer.status.update(source=f"LIVE · {port} @ {baud}")
    while True:
        try:
            with serial.Serial(port, baud, timeout=1) as ser:
                buffer.status.update(connected=True, message=f"Connected to {port}")
                while True:
                    raw = ser.readline().decode("utf-8", errors="ignore")
                    if not raw:
                        continue
                    record = parse_telemetry_line(raw)
                    if record is None:
                        continue  # invalid line, ignored safely
                    buffer.append(record)
        except Exception as exc:  # serial.SerialException, device unplugged, etc.
            buffer.status.update(connected=False, message=f"Disconnected ({exc}) — retrying...")
            time.sleep(2)


def replay_reader_loop(buffer, filepath, interval=REPLAY_PING_INTERVAL_S):
    """Loops a demo telemetry log continuously, feeding the buffer like a live link."""
    buffer.status.update(source=f"REPLAY · {filepath}")
    path = Path(filepath)
    while True:
        try:
            lines = _read_replay_lines(path)
        except FileNotFoundError:
            buffer.status.update(connected=False, message=f"Replay file not found: {filepath}")
            time.sleep(2)
            continue

        if not lines:
            buffer.status.update(connected=False, message=f"Replay file is empty: {filepath}")
            time.sleep(2)
            continue

        buffer.status.update(connected=True, message=f"Replaying {filepath} (loops continuously)")
        for line in lines:
            record = parse_telemetry_line(line)
            if record is not None:
                buffer.append(record)
            time.sleep(interval)
        # end of file reached -> loop back to the top automatically


@st.cache_resource(show_spinner=False)
def get_buffer(target: str):
    """Creates (once per target) the shared buffer and starts its reader thread."""
    buffer = TelemetryBuffer()
    if target.startswith("replay:"):
        filepath = target[len("replay:"):]
        thread = threading.Thread(
            target=replay_reader_loop, args=(buffer, filepath), daemon=True
        )
    else:
        thread = threading.Thread(
            target=serial_reader_loop, args=(buffer, target), daemon=True
        )
    thread.start()
    return buffer


@st.cache_resource(show_spinner=False)
def load_model():
    return joblib.load(MODEL_PATH)


def resolve_target() -> str:
    args = sys.argv[1:]
    if args:
        return args[0]
    return DEFAULT_REPLAY_TARGET


# ----------------------------------------------------------------------------
# ML inference (majority vote over the last N valid predictions)
# ----------------------------------------------------------------------------


def classify_latest(records, model, window=VOTE_WINDOW):
    if not records:
        return None

    recent = records[-window:]
    X = pd.DataFrame(
        [[r["temp_c"], r["tds_ppm"], r["clarity"]] for r in recent], columns=FEATURES
    )
    preds = model.predict(X)
    probas = model.predict_proba(X)
    classes = list(model.classes_)

    counts = Counter(preds)
    majority_class = counts.most_common(1)[0][0]
    class_idx = classes.index(majority_class)
    confidence = float(np.mean(probas[:, class_idx]))

    return {
        "label": majority_class,
        "confidence": confidence,
        "n_votes": len(recent),
        "latest_pred": preds[-1],
        "latest_proba": dict(zip(classes, probas[-1])),
    }


# ----------------------------------------------------------------------------
# Styling
# ----------------------------------------------------------------------------

CUSTOM_CSS = """
<style>
html, body, [class*="css"] { font-family: "IBM Plex Mono", "Consolas", monospace; }
.dhvani-header {
    display: flex; justify-content: space-between; align-items: baseline;
    border-bottom: 1px solid #1F2A37; padding-bottom: 10px; margin-bottom: 18px;
}
.dhvani-title { font-size: 1.7rem; font-weight: 700; color: #E5E7EB; letter-spacing: 0.02em; }
.dhvani-subtitle { font-size: 0.85rem; color: #6B7280; }
.status-pill {
    font-size: 0.78rem; padding: 3px 10px; border-radius: 999px;
    border: 1px solid currentColor; display: inline-block;
}
.section-label {
    font-size: 0.75rem; letter-spacing: 0.12em; color: #9CA3AF;
    text-transform: uppercase; margin: 22px 0 8px 0; font-weight: 600;
}
.ml-card {
    border-radius: 14px; padding: 26px 30px; background: #111827;
    border: 1px solid #1F2A37; border-left: 6px solid var(--accent, #38BDF8);
}
.ml-card .ml-kicker { font-size: 0.78rem; color: #9CA3AF; letter-spacing: 0.1em; text-transform: uppercase; }
.ml-card .ml-label { font-size: 2.1rem; font-weight: 700; color: #F9FAFB; margin: 4px 0 10px 0; }
.ml-card .ml-conf { font-size: 1.05rem; color: #D1D5DB; }
.ml-card .ml-conf b { color: var(--accent, #38BDF8); font-size: 1.2rem; }
.metric-box {
    background: #111827; border: 1px solid #1F2A37; border-radius: 10px;
    padding: 14px 16px;
}
.metric-box .m-label { font-size: 0.72rem; color: #9CA3AF; text-transform: uppercase; letter-spacing: 0.08em; }
.metric-box .m-value { font-size: 1.4rem; font-weight: 700; color: #F3F4F6; margin-top: 2px; }
.metric-box .m-unit { font-size: 0.8rem; color: #6B7280; margin-left: 4px; }
.power-badge {
    font-weight: 700; padding: 2px 10px; border-radius: 6px; font-size: 0.95rem;
}
</style>
"""


def metric_box(label, value, unit="", color=None):
    style = f'style="color:{color}"' if color else ""
    st.markdown(
        f"""<div class="metric-box">
            <div class="m-label">{label}</div>
            <div class="m-value" {style}>{value}<span class="m-unit">{unit}</span></div>
        </div>""",
        unsafe_allow_html=True,
    )


# ----------------------------------------------------------------------------
# Page
# ----------------------------------------------------------------------------

st.set_page_config(
    page_title="DHVANI · Ground Station",
    page_icon="〰️",
    layout="wide",
)
st.markdown(CUSTOM_CSS, unsafe_allow_html=True)

target = resolve_target()
buffer = get_buffer(target)
model = load_model()

records = buffer.snapshot()
status = buffer.status
latest = records[-1] if records else None

connected = status.get("connected", False)
status_color = "#22C55E" if connected else "#EF4444"
status_text = "CONNECTED" if connected else "WAITING"

header_l, header_r = st.columns([3, 2])
with header_l:
    st.markdown(
        """<div class="dhvani-header">
            <div>
                <div class="dhvani-title">DHVANI · Ground Station</div>
                <div class="dhvani-subtitle">Energy-aware adaptive sonar transmitter — ML water-condition inference</div>
            </div>
        </div>""",
        unsafe_allow_html=True,
    )
with header_r:
    st.markdown(
        f"""<div style="text-align:right; padding-top:4px;">
            <span class="status-pill" style="color:{status_color}">● {status_text}</span>
            &nbsp;&nbsp;
            <span class="dhvani-subtitle">{status.get('source') or target}</span><br/>
            <span class="dhvani-subtitle">{status.get('message','')}</span><br/>
            <span class="dhvani-subtitle">pings buffered: {len(records)}</span>
        </div>""",
        unsafe_allow_html=True,
    )

# ----------------------------------------------------------------------------
# A. Water condition (ML)
# ----------------------------------------------------------------------------

st.markdown('<div class="section-label">Water Condition — ML</div>', unsafe_allow_html=True)

result = classify_latest(records, model)
if result is None:
    st.markdown(
        """<div class="ml-card" style="--accent:#6B7280;">
            <div class="ml-kicker">Water Condition (ML)</div>
            <div class="ml-label">Waiting for telemetry...</div>
            <div class="ml-conf">No valid pings received yet.</div>
        </div>""",
        unsafe_allow_html=True,
    )
else:
    label_key = result["label"]
    accent = CLASS_ACCENT.get(label_key, "#38BDF8")
    display_label = LABEL_DISPLAY.get(label_key, label_key)
    confidence_pct = result["confidence"] * 100
    st.markdown(
        f"""<div class="ml-card" style="--accent:{accent};">
            <div class="ml-kicker">Water Condition (ML)</div>
            <div class="ml-label">{display_label}</div>
            <div class="ml-conf">Confidence: <b>{confidence_pct:.0f}%</b>
                &nbsp;·&nbsp; majority vote over last {result['n_votes']} valid pings</div>
        </div>""",
        unsafe_allow_html=True,
    )

# ----------------------------------------------------------------------------
# B. Sensor values
# ----------------------------------------------------------------------------

st.markdown('<div class="section-label">Sensor Values</div>', unsafe_allow_html=True)
c1, c2, c3, c4 = st.columns(4)
with c1:
    metric_box("Temperature (temp_c)", f"{latest['temp_c']:.1f}" if latest else "—", "°C")
with c2:
    metric_box("TDS / Salinity (tds_ppm)", f"{latest['tds_ppm']:.0f}" if latest else "—", "ppm")
with c3:
    metric_box("Clarity (clarity)", f"{latest['clarity']:.0f}" if latest else "—", "counts")
with c4:
    sound_speed = latest.get("sound_speed") if latest else None
    metric_box("Sound speed", f"{sound_speed:.0f}" if sound_speed is not None else "—", "m/s")

# ----------------------------------------------------------------------------
# C. Transmitter decision
# ----------------------------------------------------------------------------

st.markdown('<div class="section-label">Transmitter Decision</div>', unsafe_allow_html=True)
d1, d2, d3, d4 = st.columns(4)
with d1:
    metric_box("Mode", latest["mode"] if latest else "—")
with d2:
    if latest and latest.get("f_lo") is not None and latest.get("f_hi") is not None:
        band = f"{latest['f_lo']/1000:.1f}–{latest['f_hi']/1000:.1f}"
    else:
        band = "—"
    metric_box("Frequency band", band, "kHz")
with d3:
    dur = latest.get("dur_ms") if latest else None
    metric_box("Pulse duration", f"{dur:.0f}" if dur is not None else "—", "ms")
with d4:
    amp = latest.get("amp") if latest else None
    # DAC count -> peak-to-peak volts at the filter output: Vpp = 2 * amp * 3.3 / 4095
    amp_vpp = (2 * amp * 3.3 / 4095) if amp is not None else None
    metric_box("Amplitude", f"{amp_vpp:.2f}" if amp_vpp is not None else "—", "Vpp")

# ----------------------------------------------------------------------------
# D. Power / battery
# ----------------------------------------------------------------------------

st.markdown('<div class="section-label">Power / Battery</div>', unsafe_allow_html=True)
e1, e2, e3, e4 = st.columns(4)
with e1:
    i_ma = latest.get("i_ma") if latest else None
    metric_box("Current", f"{i_ma:.2f}" if i_ma is not None else "—", "mA")
with e2:
    e_mj = latest.get("e_mj") if latest else None
    metric_box("Energy per ping", f"{e_mj:.1f}" if e_mj is not None else "—", "mJ")
with e3:
    batt = latest.get("batt_pct") if latest else None
    metric_box("Battery budget", f"{batt:.0f}" if batt is not None else "—", "%")
with e4:
    power_mode = latest.get("power_mode") if latest else None
    color = POWER_MODE_COLOR.get(power_mode, "#9CA3AF")
    value_html = (
        f'<span class="power-badge" style="background:{color}22;color:{color};">{power_mode}</span>'
        if power_mode
        else "—"
    )
    st.markdown(
        f"""<div class="metric-box">
            <div class="m-label">Power mode</div>
            <div class="m-value">{value_html}</div>
        </div>""",
        unsafe_allow_html=True,
    )

# ----------------------------------------------------------------------------
# E. Live graphs
# ----------------------------------------------------------------------------

st.markdown('<div class="section-label">Live Graphs (last 60s)</div>', unsafe_allow_html=True)

now = time.time()
recent_window = [r for r in records if now - r["t_wall"] <= GRAPH_WINDOW_SECONDS]

g1, g2 = st.columns(2)

if recent_window:
    df = pd.DataFrame(recent_window)
    df["seconds_ago"] = df["t_wall"] - now

    with g1:
        chart = (
            alt.Chart(df)
            .mark_line(point=True, color=CLARITY_COLOR, strokeWidth=2)
            .encode(
                x=alt.X("seconds_ago:Q", title="seconds ago", scale=alt.Scale(domain=[-GRAPH_WINDOW_SECONDS, 0])),
                y=alt.Y("clarity:Q", title="clarity (counts)"),
                tooltip=[alt.Tooltip("clarity:Q", title="clarity"), alt.Tooltip("seconds_ago:Q", title="s ago", format=".1f")],
            )
            .properties(height=260, title="Clarity")
            .configure_view(strokeWidth=0)
            .configure_axis(gridColor="#1F2A37", domainColor="#374151", labelColor="#9CA3AF", titleColor="#9CA3AF")
            .configure_title(color="#E5E7EB")
        )
        st.altair_chart(chart, use_container_width=True)

    with g2:
        df_freq = df.dropna(subset=["f_hi"])
        if not df_freq.empty:
            chart2 = (
                alt.Chart(df_freq)
                .mark_line(point=True, color=FREQ_COLOR, strokeWidth=2)
                .encode(
                    x=alt.X("seconds_ago:Q", title="seconds ago", scale=alt.Scale(domain=[-GRAPH_WINDOW_SECONDS, 0])),
                    y=alt.Y("f_hi:Q", title="chirp top frequency (Hz)"),
                    tooltip=[alt.Tooltip("f_hi:Q", title="f_hi (Hz)"), alt.Tooltip("seconds_ago:Q", title="s ago", format=".1f")],
                )
                .properties(height=260, title="Chirp top frequency")
                .configure_view(strokeWidth=0)
                .configure_axis(gridColor="#1F2A37", domainColor="#374151", labelColor="#9CA3AF", titleColor="#9CA3AF")
                .configure_title(color="#E5E7EB")
            )
            st.altair_chart(chart2, use_container_width=True)
        else:
            st.info("No frequency data in the last 60 seconds.")
else:
    with g1:
        st.info("Waiting for telemetry to populate the clarity graph...")
    with g2:
        st.info("Waiting for telemetry to populate the frequency graph...")

st.caption(
    "Model: dhvani_model.joblib (RandomForestClassifier) trained on a SYNTHETIC "
    "development dataset — see ml/generate_dataset.py and dhvani_model_report.txt. "
    "Not validated against real-world sensor accuracy."
)

# ----------------------------------------------------------------------------
# Auto-refresh (serial/replay reading runs on an independent background thread)
# ----------------------------------------------------------------------------

time.sleep(UI_REFRESH_INTERVAL_S)
st.rerun()
