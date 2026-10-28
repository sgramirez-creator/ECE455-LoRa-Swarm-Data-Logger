#!/usr/bin/env bash
# Start/stop the Swarm dashboard on any laptop: the receiver (talks to whichever Meshtastic node is plugged in
# over USB, so that node becomes the base station) plus the Streamlit web app.
#
#   ./dashboard.sh start [serial-port]   start both and open the browser (radio auto-detected if no port given)
#   ./dashboard.sh stop                  stop both
#   ./dashboard.sh restart [serial-port]
#   ./dashboard.sh status
#   ./dashboard.sh logs                  follow both logs (Ctrl-C to leave)

set -euo pipefail
cd "$(dirname "$0")"

WEB_PORT="${DASHBOARD_PORT:-8501}"
LOG_DIR="logs"
mkdir -p "$LOG_DIR"

pid_of() {
    case "$1" in
    receiver) pgrep -f "[Pp]ython.* receiver\.py" | head -1 || true ;;
    supervisor) pgrep -f "dashboard\.sh _supervise" | head -1 || true ;;
    dashboard) pgrep -f "streamlit run app.py" | head -1 || true ;;
    esac
}

ensure_venv() {
    if [ ! -x .venv/bin/python ]; then
        echo "First run on this computer: creating .venv and installing requirements (takes a minute)..."
        python3 -m venv .venv
        .venv/bin/pip install -q --upgrade pip
        .venv/bin/pip install -q -r requirements.txt
    fi
}

meshtastic_ports() {
    .venv/bin/python -c "from meshtastic.util import findPorts; print('\n'.join(findPorts(True)))" 2>/dev/null
}

start_receiver() {
    local port="${1:-}"
    if [ -n "$(pid_of supervisor)" ]; then
        echo "Receiver already running (pid $(pid_of receiver))."
        return
    fi
    if [ -z "$port" ]; then
        local ports
        ports="$(meshtastic_ports)"
        if [ "$(printf '%s\n' "$ports" | grep -c .)" -gt 1 ]; then
            echo "Several Meshtastic nodes are plugged in. Pick the base station:"
            printf '  ./dashboard.sh start %s\n' $ports
            return
        fi
    fi
    if [ -n "$port" ] && lsof "$port" >/dev/null 2>&1; then
        echo "$port is in use by another program. Close the Meshtastic app or serial monitor, then retry:"
        lsof "$port" | tail -n +2
        return
    fi
    # The supervisor restarts the receiver if it ever exits; the receiver itself waits for a board to be plugged in.
    nohup "$PWD/dashboard.sh" _supervise "$port" >"$LOG_DIR/receiver.log" 2>&1 &
    for _ in $(seq 1 30); do
        grep -qE "Waiting for data|not reachable" "$LOG_DIR/receiver.log" 2>/dev/null && break
        sleep 0.5
    done
    if grep -q "Waiting for data" "$LOG_DIR/receiver.log" 2>/dev/null; then
        echo "Receiver started: $(grep -m1 'Base station:' "$LOG_DIR/receiver.log")"
    else
        echo "Receiver started; no Meshtastic board on USB yet. Plug one in and it connects automatically."
    fi
}

start_dashboard() {
    if [ -n "$(pid_of dashboard)" ]; then
        echo "Dashboard already running (pid $(pid_of dashboard))."
    else
        nohup .venv/bin/streamlit run app.py --server.headless true --server.port "$WEB_PORT" \
            >"$LOG_DIR/dashboard.log" 2>&1 &
        for _ in $(seq 1 40); do
            curl -s -o /dev/null "http://localhost:$WEB_PORT" && break
            sleep 0.5
        done
        echo "Dashboard started."
    fi
    echo "Open http://localhost:$WEB_PORT"
    # Only `start` opens a tab; `restart` reuses the one already open (it reconnects on its own).
    if [ "${1:-open}" = "open" ] && command -v open >/dev/null; then open "http://localhost:$WEB_PORT"; fi
}

stop_one() {
    local name="$1" pid
    pid="$(pid_of "$name")"
    if [ -z "$pid" ]; then
        echo "$name not running."
        return
    fi
    # SIGTERM, not Ctrl-C: background jobs ignore SIGINT.
    kill -TERM "$pid" 2>/dev/null || true
    for _ in $(seq 1 20); do
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.25
    done
    if kill -0 "$pid" 2>/dev/null; then kill -KILL "$pid" 2>/dev/null || true; fi
    echo "$name stopped."
}

stop_receiver() {
    # Stop the supervisor first so it doesn't restart the receiver we're about to stop.
    local supervisor
    supervisor="$(pid_of supervisor)"
    if [ -n "$supervisor" ]; then kill -TERM "$supervisor" 2>/dev/null || true; fi
    stop_one receiver
}

status() {
    local name pid
    for name in receiver dashboard; do
        pid="$(pid_of "$name")"
        if [ -n "$pid" ]; then echo "$name: running (pid $pid)"; else echo "$name: stopped"; fi
    done
    if [ -n "$(pid_of dashboard)" ]; then echo "dashboard: http://localhost:$WEB_PORT"; fi
    if [ -f "$LOG_DIR/receiver.log" ]; then
        echo "latest receiver output:"
        grep -vE "NotOpenSSLWarning|warnings.warn" "$LOG_DIR/receiver.log" | tail -3 | cut -c1-160
    fi
}

case "${1:-}" in
start)
    ensure_venv
    start_receiver "${2:-}"
    start_dashboard
    ;;
stop)
    stop_receiver
    stop_one dashboard
    ;;
restart)
    stop_receiver
    stop_one dashboard
    ensure_venv
    start_receiver "${2:-}"
    start_dashboard no-open
    ;;
status) status ;;
_supervise)
    # The receiver exiting non-zero must not end this loop.
    set +e
    while true; do
        .venv/bin/python -u receiver.py ${2:+--port "$2"}
        echo "Receiver exited (code $?); restarting in 3 s"
        sleep 3
    done
    ;;
logs) tail -f "$LOG_DIR/receiver.log" "$LOG_DIR/dashboard.log" ;;
*)
    sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
