#!/bin/sh
set -eu

printf '%s\n' "Starting Lego Clawd bridge..." >&2

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
PYTHON_BIN="${PYTHON_BIN:-$HOME/.platformio/penv/bin/python}"
LOG_FILE="${LEGO_CLAWD_LOG:-$PROJECT_ROOT/.lego-clawd/bridge.log}"

mkdir -p "$(dirname -- "$LOG_FILE")"
: >> "$LOG_FILE"

log() {
  printf '%s\n' "$*" >&2
  printf '%s %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*" >> "$LOG_FILE"
}

find_port() {
  for pattern in \
    "/dev/cu.usbmodem*" \
    "/dev/cu.usbserial*" \
    "/dev/cu.wchusbserial*" \
    "/dev/cu.SLAB_USBtoUART*"
  do
    for candidate in $pattern
    do
      if [ -e "$candidate" ]; then
        printf '%s\n' "$candidate"
        return 0
      fi
    done
  done

  return 1
}

PORT=${LEGO_CLAWD_PORT:-}
NEEDS_PORT=1
for arg in "$@"
do
  case "$arg" in
    --help|-h|--list-states|--dry-run)
      NEEDS_PORT=0
      ;;
  esac
done

if [ -z "$PORT" ] && [ "$NEEDS_PORT" -eq 1 ]; then
  log "Searching for ESP32 serial port..."
  if ! PORT=$(find_port); then
    log "No ESP32 serial port found. Connect the board, then run: pio device list"
    exit 1
  fi
fi

log "Lego Clawd bridge"
log "Project: $PROJECT_ROOT"
if [ -n "$PORT" ]; then
  log "Port:    $PORT"
else
  log "Port:    not required for this command"
fi
log "Python:  $PYTHON_BIN"
log "State poll: ${LEGO_CLAWD_STATE_INTERVAL:-1}s"
log "Usage refresh: ${LEGO_CLAWD_USAGE_INTERVAL:-60}s"
log "Log:     $LOG_FILE"
log "Press Ctrl-C to stop."

set -- \
  --state-interval "${LEGO_CLAWD_STATE_INTERVAL:-1}" \
  --usage-interval "${LEGO_CLAWD_USAGE_INTERVAL:-60}" \
  --log-file "$LOG_FILE" \
  "$@"

cd "$PROJECT_ROOT"
if [ -n "$PORT" ]; then
  env PYTHONUNBUFFERED=1 "$PYTHON_BIN" "$PROJECT_ROOT/tools/codexbar_bridge.py" --port "$PORT" "$@"
else
  env PYTHONUNBUFFERED=1 "$PYTHON_BIN" "$PROJECT_ROOT/tools/codexbar_bridge.py" "$@"
fi
