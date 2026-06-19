#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)

cd "$PROJECT_ROOT/macos/LegoClawdBar"
LEGO_CLAWD_PROJECT_ROOT="$PROJECT_ROOT" swift run -c release LegoClawdBar
