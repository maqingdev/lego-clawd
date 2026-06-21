#!/usr/bin/env python3
"""Start, stop, and inspect the Lego Clawd bridge as a background process."""

from __future__ import annotations

import argparse
import glob
import json
import os
import signal
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

PROJECT_ROOT = Path(__file__).resolve().parents[1]
STATE_DIR = PROJECT_ROOT / ".lego-clawd"
PID_FILE = STATE_DIR / "bridge.pid"
LOG_FILE = STATE_DIR / "bridge.log"
BRIDGE = PROJECT_ROOT / "tools/codexbar_bridge.py"
USAGE_FILE = (
    Path.home()
    / "Library/Mobile Documents/iCloud~dk~simonbs~Scriptable/Documents/codexbar-usage.json"
)
AI_STATUS_FILE = Path.home() / ".lego-clawd/ai-status.json"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Manage Lego Clawd bridge process.")
    parser.add_argument("command", choices=("start", "stop", "status", "status-json"))
    parser.add_argument("--port", help="Serial port. Defaults to first detected ESP32 port.")
    parser.add_argument("--quiet-mode", choices=("true", "false"), help="Forward quiet mode to the bridge.")
    return parser.parse_args()


def find_serial_port() -> str | None:
    patterns = (
        "/dev/cu.usbmodem*",
        "/dev/cu.usbserial*",
        "/dev/cu.wchusbserial*",
        "/dev/cu.SLAB_USBtoUART*",
    )
    ports: list[str] = []
    for pattern in patterns:
        ports.extend(glob.glob(pattern))
    return sorted(ports)[0] if ports else None


def read_pid() -> int | None:
    try:
        return int(PID_FILE.read_text(encoding="utf-8").strip())
    except (FileNotFoundError, ValueError):
        return None


def process_alive(pid: int | None) -> bool:
    if pid is None:
        return False
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


def process_command(pid: int) -> str:
    try:
        result = subprocess.run(
            ["ps", "-p", str(pid), "-o", "command="],
            check=False,
            capture_output=True,
            text=True,
            timeout=2,
        )
    except Exception:
        return ""

    if result.returncode != 0:
        return ""
    return result.stdout.strip()


def serial_owner_pids(port: str | None) -> set[int]:
    if not port:
        return set()

    try:
        result = subprocess.run(
            ["lsof", "-t", port],
            check=False,
            capture_output=True,
            text=True,
            timeout=2,
        )
    except Exception:
        return set()

    if result.returncode != 0:
        return set()

    pids: set[int] = set()
    for line in result.stdout.splitlines():
        try:
            pids.add(int(line.strip()))
        except ValueError:
            continue
    return pids


def bridge_owner_pids(port: str | None) -> set[int]:
    bridge_path = str(BRIDGE)
    pids: set[int] = set()
    for pid in serial_owner_pids(port):
        command = process_command(pid)
        if bridge_path in command or "codexbar_bridge.py" in command:
            pids.add(pid)
    return pids


def status() -> dict[str, Any]:
    pid = read_pid()
    port = find_serial_port()
    pids = bridge_owner_pids(port)
    if process_alive(pid):
        assert pid is not None
        pids.add(pid)
    else:
        try:
            PID_FILE.unlink()
        except FileNotFoundError:
            pass
    running = bool(pids)
    primary_pid = pid if pid in pids else (sorted(pids)[0] if pids else None)
    return {
        "running": running,
        "pid": primary_pid,
        "pids": sorted(pids),
        "port": port,
        "log": str(LOG_FILE),
    }


def start(port: str | None, quiet_mode: str | None = None) -> int:
    current = status()
    if current["running"]:
        print(json.dumps(current))
        return 0

    port = port or find_serial_port()
    if not port:
        print("no serial port found", file=sys.stderr)
        return 2

    STATE_DIR.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["HOME"] = str(Path.home())
    env["PATH"] = "/usr/bin:/bin:/usr/sbin:/sbin"
    env["PYTHONUNBUFFERED"] = "1"

    with LOG_FILE.open("a", encoding="utf-8") as log:
        log.write(f"{time.strftime('%Y-%m-%d %H:%M:%S')} bridge-control start {port}\n")
        command = [
            sys.executable,
            str(BRIDGE),
            "--port",
            port,
            "--usage-file",
            str(USAGE_FILE),
            "--ai-status-file",
            str(AI_STATUS_FILE),
            "--state-interval",
            "1",
            "--usage-interval",
            "60",
            "--log-file",
            str(LOG_FILE),
        ]
        if quiet_mode is not None:
            command.extend(["--quiet-mode", quiet_mode])

        process = subprocess.Popen(
            command,
            cwd=PROJECT_ROOT,
            env=env,
            stdout=log,
            stderr=log,
            start_new_session=True,
        )

    PID_FILE.write_text(f"{process.pid}\n", encoding="utf-8")
    time.sleep(0.5)
    if process.poll() is not None:
        print(f"bridge exited immediately with {process.returncode}", file=sys.stderr)
        return process.returncode or 1

    print(json.dumps(status()))
    return 0


def stop() -> int:
    pid = read_pid()
    port = find_serial_port()
    pids = bridge_owner_pids(port)
    if process_alive(pid):
        assert pid is not None
        pids.add(pid)

    if not pids:
        try:
            PID_FILE.unlink()
        except FileNotFoundError:
            pass
        print(json.dumps(status()))
        return 0

    for target_pid in sorted(pids):
        try:
            os.kill(target_pid, signal.SIGTERM)
        except ProcessLookupError:
            pass

    deadline = time.monotonic() + 2
    while any(process_alive(target_pid) for target_pid in pids) and time.monotonic() < deadline:
        time.sleep(0.05)

    for target_pid in sorted(pids):
        if not process_alive(target_pid):
            continue
        try:
            os.kill(target_pid, signal.SIGKILL)
        except ProcessLookupError:
            pass

    try:
        PID_FILE.unlink()
    except FileNotFoundError:
        pass
    print(json.dumps(status()))
    return 0


def main() -> int:
    args = parse_args()
    if args.command == "start":
        return start(args.port, args.quiet_mode)
    if args.command == "stop":
        return stop()
    current = status()
    if args.command == "status-json":
        print(json.dumps(current))
    else:
        state = "running" if current["running"] else "stopped"
        print(f"{state} pid={current['pid']} port={current['port']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
