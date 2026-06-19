#!/usr/bin/env python3
"""Bridge CodexBar usage JSON to the Lego Clawd ESP32 over USB serial."""

from __future__ import annotations

import argparse
import glob
import json
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Any

DEFAULT_USAGE_FILE = (
    Path.home()
    / "Library/Mobile Documents/iCloud~dk~simonbs~Scriptable/Documents/codexbar-usage.json"
)
DEFAULT_AI_STATUS_FILE = Path.home() / ".lego-clawd/ai-status.json"
ACTIVITIES = ("idle", "working", "pending", "waiting", "error", "disconnected")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Send CodexBar usage data to Lego Clawd over serial."
    )
    parser.add_argument("--usage-file", type=Path, default=DEFAULT_USAGE_FILE)
    parser.add_argument("--port", help="Serial port, for example /dev/cu.usbmodem1101")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument(
        "--state-interval",
        type=float,
        default=1.0,
        help="Seconds between AI status checks.",
    )
    parser.add_argument(
        "--usage-interval",
        type=float,
        default=60.0,
        help="Seconds between Codex usage file refreshes.",
    )
    parser.add_argument(
        "--heartbeat-interval",
        type=float,
        default=3.0,
        help="Seconds between repeated serial updates when payload is unchanged.",
    )
    parser.add_argument("--interval", type=float, help=argparse.SUPPRESS)
    parser.add_argument("--log-file", type=Path, help="Append bridge logs to this file.")
    parser.add_argument("--once", action="store_true", help="Send/read once and exit.")
    parser.add_argument("--dry-run", action="store_true", help="Print JSON without serial.")
    parser.add_argument(
        "--state",
        choices=("idle", "working", "pending", "approval", "waiting", "done", "error",
                 "disconnected", "offline"),
        help="Override AI state. 'approval' is pending; 'done' is waiting.",
    )
    parser.add_argument(
        "--list-states",
        action="store_true",
        help="List supported AI states and aliases, then exit.",
    )
    parser.add_argument(
        "--approval-test",
        type=float,
        metavar="SECONDS",
        help="Send pending/approval, hold for SECONDS, send idle, then exit.",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="Include selfTest=true so the firmware runs its end-to-end demo.",
    )
    parser.add_argument(
        "--idle-timeout",
        type=float,
        default=60.0,
        help="Seconds after which a stale AI status falls back to idle.",
    )
    parser.add_argument(
        "--waiting",
        choices=("true", "false"),
        help="Override AI state as waiting or idle.",
    )
    parser.add_argument("--ai-status-file", type=Path, default=DEFAULT_AI_STATUS_FILE)
    parser.add_argument(
        "--waiting-file",
        type=Path,
        help="Legacy file containing true/false, 1/0, waiting/idle.",
    )
    parser.add_argument(
        "--pending-wave-forward-pulse-us",
        type=int,
        help="Temporarily override pending wave forward endpoint pulse width.",
    )
    parser.add_argument(
        "--pending-wave-pause-ms",
        type=int,
        help="Temporarily override pending wave endpoint pause in milliseconds.",
    )
    parser.add_argument(
        "--quiet-mode",
        choices=("true", "false"),
        help="Enable or disable firmware quiet mode.",
    )
    return parser.parse_args()


def emit(message: str, log_file: Path | None = None, *, error: bool = False) -> None:
    stream = sys.stderr if error else sys.stdout
    print(message, file=stream, flush=True)
    if log_file is None:
        return

    timestamp = datetime.now().astimezone().strftime("%Y-%m-%d %H:%M:%S")
    with log_file.open("a", encoding="utf-8") as file:
        file.write(f"{timestamp} {message}\n")


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


def normalize_activity(value: Any, waiting: Any = None, pending: Any = None,
                       hook_event: Any = None) -> str:
    if hook_event == "PermissionRequest" or (isinstance(pending, bool) and pending):
        return "pending"

    if isinstance(waiting, bool) and waiting:
        return "waiting"

    if isinstance(value, str):
        text = value.strip().lower()
        if text in {"idle", "ready"}:
            return "idle"
        if text in {"working", "running", "thinking"}:
            return "working"
        if text in {"pending", "approval", "waiting_approval", "asking_approval"}:
            return "pending"
        if text in {"waiting", "waiting_input", "done"}:
            return "waiting"
        if text in {"error", "err", "fault"}:
            return "error"
        if text in {"disconnected", "offline", "no_link", "lost"}:
            return "disconnected"

    if isinstance(waiting, bool):
        return "waiting" if waiting else "idle"

    return "idle"


def print_supported_states() -> None:
    print("states:")
    for activity in ACTIVITIES:
        print(f"  {activity}")
    print("aliases:")
    print("  approval -> pending")
    print("  done     -> waiting")
    print("  offline  -> disconnected")


def status_is_stale(status: dict[str, Any], timeout_seconds: float) -> bool:
    updated_at = status.get("updatedAt")
    if not isinstance(updated_at, str) or timeout_seconds <= 0:
        return False

    try:
        updated_time = datetime.fromisoformat(updated_at.replace("Z", "+00:00"))
    except ValueError:
        return False

    return (datetime.now().astimezone() - updated_time.astimezone()).total_seconds() > timeout_seconds


def load_ai_status(args: argparse.Namespace) -> dict[str, Any] | None:
    try:
        return load_json(args.ai_status_file)
    except FileNotFoundError:
        return None


def read_activity(args: argparse.Namespace, status: dict[str, Any] | None = None) -> str:
    if args.approval_test is not None:
        return "pending"

    if args.state is not None:
        return normalize_activity(args.state)

    if args.waiting_file:
        try:
            text = args.waiting_file.read_text(encoding="utf-8").strip().lower()
            return "waiting" if text in {"1", "true", "yes", "waiting", "wait"} else "idle"
        except FileNotFoundError:
            return "idle"

    if args.waiting is not None:
      return "waiting" if args.waiting == "true" else "idle"

    if status is None:
        return "idle"

    if status_is_stale(status, args.idle_timeout):
        return "idle"

    return normalize_activity(
        status.get("state") or status.get("aiState"),
        status.get("waiting"),
        status.get("pending"),
        status.get("hookEvent"),
    )


def build_usage_payload(args: argparse.Namespace) -> dict[str, Any]:
    data = load_json(args.usage_file)
    codex = codex_provider(data)
    primary = codex.get("primary")
    secondary = codex.get("secondary")

    return {
        "codex5h": percent(primary),
        "codex1w": percent(secondary),
        "reset5h": format_reset(primary),
        "reset1w": format_reset(secondary),
    }


def build_status_payload(args: argparse.Namespace) -> dict[str, Any]:
    ai_status = load_ai_status(args)
    activity = read_activity(args, ai_status)

    return {
        "aiState": activity,
        "waiting": activity == "waiting",
        "pending": activity == "pending",
        "idleIn": -1,
    }


def add_runtime_overrides(payload: dict[str, Any], args: argparse.Namespace) -> None:
    if args.pending_wave_forward_pulse_us is not None:
        payload["pendingWaveForwardPulseUs"] = args.pending_wave_forward_pulse_us
    if args.pending_wave_pause_ms is not None:
        payload["pendingWavePauseMs"] = args.pending_wave_pause_ms
    if args.quiet_mode is not None:
        payload["quietMode"] = args.quiet_mode == "true"


def build_payload(args: argparse.Namespace) -> dict[str, Any]:
    payload = {**build_usage_payload(args), **build_status_payload(args)}
    add_runtime_overrides(payload, args)
    if args.self_test:
        payload["selfTest"] = True
    return payload


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

    try:
        return serial.Serial(port, baudrate=baud, timeout=1, write_timeout=1)
    except Exception as error:
        detail = describe_serial_owner(port)
        if detail:
            raise RuntimeError(f"could not open {port}: {error}\n{detail}") from error
        raise RuntimeError(f"could not open {port}: {error}") from error


def describe_serial_owner(port: str) -> str:
    try:
        result = subprocess.run(
            ["lsof", port],
            check=False,
            capture_output=True,
            text=True,
            timeout=2,
        )
    except Exception:
        return ""

    if result.returncode == 0 and result.stdout.strip():
        return f"{port} appears to be in use:\n{result.stdout.strip()}"
    return f"No process was reported by lsof for {port}."


def main() -> int:
    args = parse_args()
    if args.list_states:
        print_supported_states()
        return 0

    if args.interval is not None:
        args.state_interval = args.interval

    port = None if args.dry_run else args.port or find_serial_port()
    if port:
        emit(f"Opening serial: {port} @ {args.baud}", args.log_file)
    serial_conn = None if args.dry_run else open_serial(port, args.baud)

    if serial_conn:
        emit("Serial opened. Waiting for board reset...", args.log_file)
        time.sleep(2.0)

    usage_payload: dict[str, Any] | None = None
    last_sent_payload: dict[str, Any] | None = None
    last_usage_refresh = 0.0
    last_sent_at = 0.0

    def send_payload(payload: dict[str, Any]) -> None:
        line = json.dumps(payload, separators=(",", ":"))

        if args.dry_run:
            emit(line, args.log_file)
            return

        assert serial_conn is not None
        assert port is not None
        serial_conn.write((line + "\n").encode("utf-8"))
        serial_conn.flush()
        emit(f"{port} <- {line}", args.log_file)

    def send_activity_once(activity: str, usage_payload: dict[str, Any]) -> None:
        payload = {
            **usage_payload,
            "aiState": activity,
            "waiting": activity == "waiting",
            "pending": activity == "pending",
            "idleIn": -1,
        }
        add_runtime_overrides(payload, args)
        send_payload(payload)

    try:
        if args.approval_test is not None:
            usage_payload = build_usage_payload(args)
            send_activity_once("pending", usage_payload)
            if not args.dry_run:
                time.sleep(max(0.0, args.approval_test))
            send_activity_once("idle", usage_payload)
            return 0

        while True:
            now = time.monotonic()
            usage_due = (
                usage_payload is None
                or args.once
                or args.usage_interval <= 0
                or now - last_usage_refresh >= args.usage_interval
            )

            if usage_due:
                usage_payload = build_usage_payload(args)
                last_usage_refresh = now

            assert usage_payload is not None
            payload = {**usage_payload, **build_status_payload(args)}
            add_runtime_overrides(payload, args)
            if args.self_test:
                payload["selfTest"] = True

            if args.once or payload != last_sent_payload:
                send_payload(payload)
                last_sent_payload = dict(payload)
                last_sent_at = now
            elif args.heartbeat_interval > 0 and now - last_sent_at >= args.heartbeat_interval:
                send_payload(payload)
                last_sent_at = now

            if args.once:
                return 0
            time.sleep(max(0.1, args.state_interval))
    finally:
        if serial_conn:
            serial_conn.close()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
    except Exception as error:
        log_path = None
        if "--log-file" in sys.argv:
            index = sys.argv.index("--log-file")
            if index + 1 < len(sys.argv):
                log_path = Path(sys.argv[index + 1])
        emit(f"codexbar bridge: {error}", log_path, error=True)
        raise SystemExit(1)
