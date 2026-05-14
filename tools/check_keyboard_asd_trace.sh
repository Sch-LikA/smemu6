#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
EMU_BIN="${EMU_BIN:-$ROOT_DIR/build/smemu6}"
FLOPPY_IMAGE="${FLOPPY_IMAGE:-$ROOT_DIR/floppies/Sys1-H.dsk}"
LOG_FILE="${LOG_FILE:-$ROOT_DIR/tmp/check_keyboard_asd_trace.log}"
TIMEOUT_SECS="${TIMEOUT_SECS:-30}"

if [[ ! -x "$EMU_BIN" ]]; then
    echo "missing emulator binary: $EMU_BIN" >&2
    exit 1
fi

if [[ ! -f "$FLOPPY_IMAGE" ]]; then
    echo "missing floppy image: $FLOPPY_IMAGE" >&2
    exit 1
fi

if ! command -v xdotool >/dev/null 2>&1; then
    echo "xdotool is required for this check" >&2
    exit 1
fi

mkdir -p "$(dirname "$LOG_FILE")"
rm -f "$LOG_FILE"

pushd "$ROOT_DIR" >/dev/null
"$EMU_BIN" -no-launcher -floppy "$FLOPPY_IMAGE" -no-display-off -tracekbd -traceflow -timeout "$TIMEOUT_SECS" 2>"$LOG_FILE" &
emu_wrapper_pid=$!
popd >/dev/null

cleanup() {
    if kill -0 "$emu_wrapper_pid" >/dev/null 2>&1; then
        kill "$emu_wrapper_pid" >/dev/null 2>&1 || true
        wait "$emu_wrapper_pid" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

emu_pid=""
for _ in $(seq 1 200); do
    if [[ -f "$LOG_FILE" ]]; then
        emu_pid="$(sed -n 's/^\[main\] PID \([0-9][0-9]*\).*/\1/p' "$LOG_FILE" | head -n1)"
        if [[ -n "$emu_pid" ]]; then
            break
        fi
    fi
    sleep 0.1
done

if [[ -z "$emu_pid" ]]; then
    echo "failed to discover emulator PID from $LOG_FILE" >&2
    exit 1
fi

inject_keys() {
    local window_id=""

    for _ in $(seq 1 100); do
        window_id="$(xdotool search --onlyvisible --pid "$emu_pid" 2>/dev/null | head -n1 || true)"
        if [[ -n "$window_id" ]] && xdotool getwindowname "$window_id" >/dev/null 2>&1; then
            if xdotool keydown --window "$window_id" a >/dev/null 2>&1 &&
               xdotool keydown --window "$window_id" s >/dev/null 2>&1 &&
               xdotool keydown --window "$window_id" d >/dev/null 2>&1 &&
               xdotool keyup --window "$window_id" a >/dev/null 2>&1 &&
               xdotool keyup --window "$window_id" s >/dev/null 2>&1 &&
               xdotool keyup --window "$window_id" d >/dev/null 2>&1; then
                return 0
            fi
        fi
        sleep 0.1
    done

    echo "failed to inject keys into SDL window for emulator PID $emu_pid" >&2
    return 1
}

inject_keys

wait "$emu_wrapper_pid"
trap - EXIT

require_match() {
    local pattern="$1"
    local description="$2"
    if ! rg -q "$pattern" "$LOG_FILE"; then
        echo "missing trace evidence: $description" >&2
        echo "log: $LOG_FILE" >&2
        exit 1
    fi
}

require_match "\\[cli-w\\] pc=590F \\[45C0\\] 2D -> 61" "visible insertion of 'a'"
require_match "\\[cli-w\\] pc=590F \\[45C1\\] 2D -> 73" "visible insertion of 's'"
require_match "\\[cli-w\\] pc=590F \\[45C2\\] 2D -> 64" "visible insertion of 'd'"

enqueue_count="$(rg -c "\\[kbd-w\\] pc=0205 \\[457C\\]" "$LOG_FILE")"
dequeue_count="$(rg -c "\\[kbd-w\\] pc=04EB \\[457C\\]" "$LOG_FILE")"
if [[ "$enqueue_count" -lt 3 || "$dequeue_count" -lt 3 ]]; then
    echo "unexpected circular-buffer activity: enqueue=$enqueue_count dequeue=$dequeue_count" >&2
    echo "log: $LOG_FILE" >&2
    exit 1
fi

echo "keyboard trace check passed"
echo "log: $LOG_FILE"