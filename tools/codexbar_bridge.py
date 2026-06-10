#!/usr/bin/env python3
"""Bridge CodexBar usage JSON to the Lego Clawd ESP32 over USB serial."""

from __future__ import annotations

import argparse
import glob
import json
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Any

DEFAULT_USAGE_FILE = (
    Path.home()
    / "Library/Mobile Documents/iCloud~dk~simonbs~Scriptable/Documents/codexbar-usage.json"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Send CodexBar usage data to Lego Clawd over serial."
    )
    parser.add_argument("--usage-file", type=Path, default=DEFAULT_USAGE_FILE)
    parser.add_argument("--port", help="Serial port, for example /dev/cu.usbmodem1101")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--interval", type=float, default=5.0)
    parser.add_argument("--once", action="store_true", help="Send/read once and exit.")
    parser.add_argument("--dry-run", action="store_true", help="Print JSON without serial.")
    parser.add_argument(
        "--waiting",
        choices=("true", "false"),
        default="false",
        help="Whether the AI is waiting for user input. Defaults to false.",
    )
    parser.add_argument(
        "--waiting-file",
        type=Path,
        help="Optional file containing true/false, 1/0, waiting/idle.",
    )
    return parser.parse_args()


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as file:
        data = json.load(file)
    if not isinstance(data, dict):
        raise ValueError(f"{path} does not contain a JSON object")
    return data


def codex_provider(data: dict[str, Any]) -> dict[str, Any]:
    providers = data.get("providers")
    if not isinstance(providers, list):
        raise ValueError("missing providers array")

    for provider in providers:
        if isinstance(provider, dict) and provider.get("provider") == "codex":
            return provider

    raise ValueError("missing provider == codex")


def percent(window: Any) -> int:
    if not isinstance(window, dict):
        return 100

    value = window.get("leftPercent")
    if value is None:
        used = window.get("usedPercent")
        value = 100 - used if isinstance(used, (int, float)) else 100

    return max(0, min(100, int(round(float(value)))))


def format_reset(window: Any) -> str:
    if not isinstance(window, dict):
        return "--:--"

    resets_at = window.get("resetsAt")
    if isinstance(resets_at, str) and resets_at:
        try:
            reset_time = datetime.fromisoformat(resets_at.replace("Z", "+00:00"))
            local_time = reset_time.astimezone()
            now = datetime.now().astimezone()
            if local_time.date() == now.date():
                return local_time.strftime("%H:%M")
            return local_time.strftime("%b %d %H:%M")
        except ValueError:
            pass

    description = window.get("resetDescription")
    if isinstance(description, str) and description:
        return description.replace("Resets ", "")

    return "--:--"


def read_waiting(args: argparse.Namespace) -> bool:
    if args.waiting_file:
        try:
            text = args.waiting_file.read_text(encoding="utf-8").strip().lower()
            return text in {"1", "true", "yes", "waiting", "wait"}
        except FileNotFoundError:
            return False

    return args.waiting == "true"


def build_payload(args: argparse.Namespace) -> dict[str, Any]:
    data = load_json(args.usage_file)
    codex = codex_provider(data)
    primary = codex.get("primary")
    secondary = codex.get("secondary")

    return {
        "codex5h": percent(primary),
        "codex1w": percent(secondary),
        "reset5h": format_reset(primary),
        "reset1w": format_reset(secondary),
        "waiting": read_waiting(args),
    }


def find_serial_port() -> str:
    patterns = (
        "/dev/cu.usbmodem*",
        "/dev/cu.usbserial*",
        "/dev/cu.wchusbserial*",
        "/dev/cu.SLAB_USBtoUART*",
    )
    ports: list[str] = []
    for pattern in patterns:
        ports.extend(glob.glob(pattern))
    if not ports:
        raise RuntimeError("no ESP32 serial port found; pass --port /dev/cu....")
    return sorted(ports)[0]


def open_serial(port: str, baud: int):
    try:
        import serial  # type: ignore
    except ImportError as error:
        raise RuntimeError(
            "pyserial is required. Run with ~/.platformio/penv/bin/python "
            "or install pyserial for your Python."
        ) from error

    return serial.Serial(port, baudrate=baud, timeout=1, write_timeout=1)


def main() -> int:
    args = parse_args()
    port = None if args.dry_run else args.port or find_serial_port()
    serial_conn = None if args.dry_run else open_serial(port, args.baud)

    if serial_conn:
        time.sleep(2.0)

    try:
        while True:
            payload = build_payload(args)
            line = json.dumps(payload, separators=(",", ":"))

            if args.dry_run:
                print(line)
            else:
                assert serial_conn is not None
                assert port is not None
                serial_conn.write((line + "\n").encode("utf-8"))
                serial_conn.flush()
                print(f"{port} <- {line}")

            if args.once:
                return 0
            time.sleep(args.interval)
    finally:
        if serial_conn:
            serial_conn.close()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
    except Exception as error:
        print(f"codexbar bridge: {error}", file=sys.stderr)
        raise SystemExit(1)
