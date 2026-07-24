#!/usr/bin/env python3
"""Read Codex usage from lightweight local sources.

The bridge sends only the legacy serial payload to the ESP32. This module keeps
source-specific details here and normalizes everything into one stable shape.
"""

from __future__ import annotations

import json
import os
import select
import shutil
import subprocess
import time
import urllib.error
import urllib.request
from datetime import datetime
from pathlib import Path
from typing import Any, Callable

PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_USAGE_STATE_FILE = PROJECT_ROOT / ".lego-clawd/usage-state.json"
USAGE_SOURCES = ("auto", "codex-cli-rpc", "codex-auth")


class UsageError(RuntimeError):
    pass


def write_json_atomic(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp_path = path.with_suffix(path.suffix + ".tmp")
    tmp_path.write_text(json.dumps(data, separators=(",", ":")) + "\n", encoding="utf-8")
    tmp_path.replace(path)


def read_usage(args: Any, emit: Callable[..., None] | None = None) -> dict[str, Any]:
    source = getattr(args, "usage_source", "auto")
    if source not in USAGE_SOURCES:
        raise UsageError(f"unsupported usage source: {source}")

    sources = [source] if source != "auto" else ["codex-cli-rpc", "codex-auth"]
    errors: list[str] = []
    for candidate in sources:
        try:
            usage = _read_source(candidate, args)
            usage["source"] = candidate
            usage["updatedAt"] = now_iso()
            usage["stale"] = False
            usage["error"] = None
            _write_usage_state(args, usage)
            return usage
        except Exception as error:
            detail = f"{candidate}: {error}"
            errors.append(detail)
            if emit is not None:
                emit(f"usage source failed: {detail}", getattr(args, "log_file", None), error=True)

    cached = _read_cached_usage(args)
    if cached is not None:
        cached["stale"] = True
        cached["error"] = "; ".join(errors)
        _write_usage_state(args, cached)
        return cached

    raise UsageError("; ".join(errors) if errors else "no usage source available")


def to_serial_payload(usage: dict[str, Any]) -> dict[str, Any]:
    five_hour = usage.get("fiveHour")
    weekly = usage.get("weekly")
    return {
        "codex5h": remaining_percent(five_hour),
        "codex1w": remaining_percent(weekly),
        "reset5h": format_reset(five_hour, "time"),
        "reset1w": format_reset(weekly, "date"),
    }


def _read_source(source: str, args: Any) -> dict[str, Any]:
    if source == "codex-cli-rpc":
        return read_codex_cli_rpc(args)
    if source == "codex-auth":
        return read_codex_auth(args)
    raise UsageError(f"unsupported usage source: {source}")


def read_codex_cli_rpc(args: Any) -> dict[str, Any]:
    codex_cli = find_codex_cli(getattr(args, "codex_cli", None))
    timeout = max(1.0, float(getattr(args, "codex_rpc_timeout", 5.0)))
    request_timeout_at = time.monotonic() + timeout

    process = subprocess.Popen(
        [codex_cli, "app-server", "--stdio"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=codex_cli_environment(codex_cli),
    )
    try:
        _rpc_send(
            process,
            {
                "jsonrpc": "2.0",
                "id": 1,
                "method": "initialize",
                "params": {
                    "clientInfo": {"name": "lego-clawd", "version": "0.1.0"},
                    "capabilities": {},
                },
            },
        )
        _rpc_response(process, 1, request_timeout_at)
        _rpc_send(process, {"jsonrpc": "2.0", "method": "initialized"})
        _rpc_send(
            process,
            {"jsonrpc": "2.0", "id": 2, "method": "account/rateLimits/read", "params": None},
        )
        response = _rpc_response(process, 2, request_timeout_at)
        if not isinstance(response, dict):
            raise UsageError("RPC response is not a JSON object")
        if "error" in response:
            raise UsageError(f"RPC error: {response['error']}")
        result = response.get("result")
        if not isinstance(result, dict):
            raise UsageError("RPC response missing result")
        return normalize_rate_limits(result)
    finally:
        _terminate_process(process)


def find_codex_cli(explicit_path: str | None = None) -> str:
    override = explicit_path or os.environ.get("CODEX_CLI")
    if override:
        return override

    candidates = [
        shutil.which("codex"),
        "/opt/homebrew/bin/codex",
        "/usr/local/bin/codex",
        str(Path.home() / ".local/bin/codex"),
        "/Applications/Codex.app/Contents/Resources/codex",
        "/Applications/ChatGPT.app/Contents/Resources/codex",
    ]
    for candidate in candidates:
        if candidate and os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
    return "codex"


def codex_cli_environment(codex_cli: str) -> dict[str, str]:
    environment = os.environ.copy()
    path_entries: list[str] = []
    if os.path.isabs(codex_cli):
        path_entries.append(str(Path(codex_cli).parent))
    path_entries.extend(("/opt/homebrew/bin", "/usr/local/bin"))
    path_entries.extend(environment.get("PATH", "").split(os.pathsep))
    environment["PATH"] = os.pathsep.join(dict.fromkeys(entry for entry in path_entries if entry))
    return environment


def read_codex_auth(args: Any) -> dict[str, Any]:
    auth_path = find_auth_json(getattr(args, "codex_home", None))
    auth = load_json(auth_path)
    if not _has_codex_login(auth):
        raise UsageError(f"{auth_path} does not contain a usable Codex login")

    tokens = auth.get("tokens")
    access_token = tokens.get("access_token") if isinstance(tokens, dict) else auth.get("access_token")
    account_id = tokens.get("account_id") if isinstance(tokens, dict) else auth.get("chatgpt_account_id")
    if not isinstance(access_token, str) or not access_token:
        raise UsageError(f"{auth_path} is missing an access token")

    url = os.environ.get("CODEX_USAGE_URL", "https://chatgpt.com/backend-api/wham/usage")
    headers = {
        "Authorization": f"Bearer {access_token}",
        "Accept": "application/json",
        "User-Agent": "lego-clawd",
    }
    if isinstance(account_id, str) and account_id:
        headers["ChatGPT-Account-ID"] = account_id

    request = urllib.request.Request(url, headers=headers)
    timeout = max(1.0, float(getattr(args, "codex_auth_timeout", 10.0)))
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            data = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        raise UsageError(f"usage endpoint returned HTTP {error.code}") from error
    except urllib.error.URLError as error:
        raise UsageError(f"usage endpoint failed: {error.reason}") from error

    if not isinstance(data, dict):
        raise UsageError("usage endpoint did not return a JSON object")
    return normalize_auth_usage(data)


def normalize_rate_limits(data: dict[str, Any]) -> dict[str, Any]:
    snapshot = data.get("rateLimits")
    by_limit_id = data.get("rateLimitsByLimitId")
    if isinstance(by_limit_id, dict) and isinstance(by_limit_id.get("codex"), dict):
        snapshot = by_limit_id["codex"]
    if not isinstance(snapshot, dict):
        raise UsageError("missing rateLimits snapshot")
    usage = normalize_windows(snapshot.get("primary"), snapshot.get("secondary"))
    usage["resetCredits"] = normalize_reset_credits(data.get("rateLimitResetCredits"))
    return usage


def normalize_auth_usage(data: dict[str, Any]) -> dict[str, Any]:
    rate_limit = data.get("rate_limit")
    if not isinstance(rate_limit, dict):
        raise UsageError("usage endpoint response missing rate_limit")
    usage = normalize_windows(rate_limit.get("primary_window"), rate_limit.get("secondary_window"))
    usage["resetCredits"] = normalize_reset_credits(data.get("rate_limit_reset_credits"))
    return usage


def normalize_windows(primary: Any, secondary: Any) -> dict[str, Any]:
    windows: dict[str, dict[str, Any] | None] = {
        "fiveHour": None,
        "weekly": None,
    }
    unclassified: list[tuple[str, dict[str, Any]]] = []

    for fallback_name, window in (("fiveHour", primary), ("weekly", secondary)):
        if not isinstance(window, dict):
            continue
        duration = window_duration_minutes(window)
        if duration is None:
            unclassified.append((fallback_name, window))
            continue
        window_name = "weekly" if duration >= 24 * 60 else "fiveHour"
        if windows[window_name] is None:
            windows[window_name] = window
        else:
            unclassified.append((fallback_name, window))

    for fallback_name, window in unclassified:
        if windows[fallback_name] is None:
            windows[fallback_name] = window
            continue
        other_name = "weekly" if fallback_name == "fiveHour" else "fiveHour"
        if windows[other_name] is None:
            windows[other_name] = window

    return {
        "fiveHour": normalize_window(windows["fiveHour"]),
        "weekly": normalize_window(windows["weekly"]),
    }


def window_duration_minutes(window: dict[str, Any]) -> float | None:
    minutes = number_value(
        window,
        "windowDurationMins",
        "window_duration_mins",
        "windowDurationMinutes",
        "window_duration_minutes",
    )
    if minutes is not None:
        return minutes

    seconds = number_value(
        window,
        "limitWindowSeconds",
        "limit_window_seconds",
        "windowDurationSeconds",
        "window_duration_seconds",
    )
    if seconds is not None:
        return seconds / 60
    return None


def normalize_window(window: Any) -> dict[str, Any] | None:
    if not isinstance(window, dict):
        return None

    used = number_value(window, "usedPercent", "used_percent")
    remaining = number_value(window, "remainingPercent", "remaining_percent")
    if remaining is None and used is not None:
        remaining = 100 - used
    if used is None and remaining is not None:
        used = 100 - remaining

    return {
        "usedPercent": clamp_percent(used),
        "remainingPercent": clamp_percent(remaining, fallback=100),
        "resetAt": normalize_reset_at(value_for(window, "resetsAt", "resets_at", "resetAt", "reset_at")),
        "durationMinutes": window_duration_minutes(window),
    }


def normalize_reset_credits(value: Any) -> dict[str, Any]:
    if not isinstance(value, dict):
        return {
            "availableCount": None,
            "expiresAt": None,
            "scope": "unknown",
            "credits": None,
        }

    available = value_for(value, "availableCount", "available_count")
    if isinstance(available, str) and available:
        try:
            available = int(available)
        except ValueError:
            available = None
    if not isinstance(available, int):
        available = None

    raw_credits = value_for(value, "credits")
    credits = None
    if isinstance(raw_credits, list):
        credits = [
            normalize_reset_credit(credit)
            for credit in raw_credits
            if isinstance(credit, dict)
        ]

    return {
        "availableCount": available,
        "expiresAt": normalize_reset_at(value_for(value, "expiresAt", "expires_at")),
        "scope": value_for(value, "scope") or "unknown",
        "credits": credits,
    }


def normalize_reset_credit(value: dict[str, Any]) -> dict[str, Any]:
    return {
        "id": value_for(value, "id"),
        "title": value_for(value, "title"),
        "description": value_for(value, "description"),
        "status": value_for(value, "status") or "unknown",
        "resetType": value_for(value, "resetType", "reset_type") or "unknown",
        "grantedAt": normalize_reset_at(value_for(value, "grantedAt", "granted_at")),
        "expiresAt": normalize_reset_at(value_for(value, "expiresAt", "expires_at")),
    }


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as file:
        data = json.load(file)
    if not isinstance(data, dict):
        raise UsageError(f"{path} does not contain a JSON object")
    return data


def find_auth_json(codex_home: str | Path | None = None) -> Path:
    base = Path(codex_home) if codex_home else Path(os.environ.get("CODEX_HOME", Path.home() / ".codex"))
    path = base / "auth.json"
    if path.exists():
        return path
    fallback = Path.home() / ".codex/auth.json"
    if fallback.exists():
        return fallback
    raise UsageError(f"missing Codex auth file at {path}")


def _has_codex_login(auth: dict[str, Any]) -> bool:
    return any(
        key in auth
        for key in (
            "OPENAI_API_KEY",
            "api_key",
            "tokens",
            "access_token",
            "refresh_token",
            "chatgpt_account_id",
        )
    )


def _read_cached_usage(args: Any) -> dict[str, Any] | None:
    path = getattr(args, "usage_state_file", DEFAULT_USAGE_STATE_FILE)
    try:
        data = load_json(path)
    except (FileNotFoundError, UsageError, OSError, json.JSONDecodeError):
        return None
    if not all(key in data for key in ("fiveHour", "weekly")):
        return None
    windows = (data.get("fiveHour"), data.get("weekly"))
    if all(window is None or isinstance(window, dict) for window in windows) and any(
        isinstance(window, dict) for window in windows
    ):
        return data
    return None


def _write_usage_state(args: Any, usage: dict[str, Any]) -> None:
    path = getattr(args, "usage_state_file", None)
    if path is None:
        return
    try:
        write_json_atomic(path, usage)
    except OSError:
        pass


def value_for(data: dict[str, Any], *keys: str) -> Any:
    for key in keys:
        if key in data:
            return data[key]
    return None


def number_value(data: dict[str, Any], *keys: str) -> float | None:
    value = value_for(data, *keys)
    if isinstance(value, (int, float)):
        return float(value)
    if isinstance(value, str) and value:
        try:
            return float(value)
        except ValueError:
            return None
    return None


def clamp_percent(value: float | None, fallback: int | None = None) -> int | None:
    if value is None:
        return fallback
    return max(0, min(100, int(round(value))))


def remaining_percent(window: Any) -> int:
    if not isinstance(window, dict):
        return 100
    value = window.get("remainingPercent")
    if isinstance(value, int):
        return max(0, min(100, value))
    return 100


def normalize_reset_at(value: Any) -> str | None:
    if isinstance(value, (int, float)):
        return datetime.fromtimestamp(value).astimezone().isoformat(timespec="seconds")
    if isinstance(value, str) and value:
        try:
            return datetime.fromisoformat(value.replace("Z", "+00:00")).astimezone().isoformat(timespec="seconds")
        except ValueError:
            return value
    return None


def format_reset(window: Any, display: str) -> str:
    if not isinstance(window, dict):
        return "--:--"

    reset_at = window.get("resetAt")
    if isinstance(reset_at, str) and reset_at:
        try:
            local_time = datetime.fromisoformat(reset_at.replace("Z", "+00:00")).astimezone()
            if display == "time":
                return local_time.strftime("%H:%M")
            return local_time.strftime("%b %d")
        except ValueError:
            if reset_at.startswith("Resets "):
                return reset_at.replace("Resets ", "")
            return reset_at

    return "--:--"


def now_iso() -> str:
    return datetime.now().astimezone().isoformat(timespec="seconds")


def _rpc_send(process: subprocess.Popen[bytes], message: dict[str, Any]) -> None:
    if process.stdin is None:
        raise UsageError("RPC stdin is closed")
    process.stdin.write(json.dumps(message, separators=(",", ":")).encode("utf-8") + b"\n")
    process.stdin.flush()


def _rpc_response(process: subprocess.Popen[bytes], request_id: int, timeout_at: float) -> dict[str, Any]:
    while time.monotonic() < timeout_at:
        message = _rpc_read_message(process, timeout_at)
        if not isinstance(message, dict):
            continue
        if message.get("id") == request_id:
            return message
    raise UsageError("RPC timed out")


def _rpc_read_message(process: subprocess.Popen[bytes], timeout_at: float) -> Any:
    if process.stdout is None:
        raise UsageError("RPC stdout is closed")

    line = _readline_with_timeout(process, timeout_at)
    if not line:
        stderr = _process_stderr(process)
        raise UsageError(stderr or "RPC closed stdout")

    if line.lower().startswith(b"content-length:"):
        length = int(line.split(b":", 1)[1].strip())
        while True:
            header = _readline_with_timeout(process, timeout_at)
            if header in {b"\r\n", b"\n", b""}:
                break
        body = _read_exact(process, length, timeout_at)
        return json.loads(body.decode("utf-8"))

    return json.loads(line.decode("utf-8"))


def _readline_with_timeout(process: subprocess.Popen[bytes], timeout_at: float) -> bytes:
    while time.monotonic() < timeout_at:
        if process.poll() is not None:
            break
        assert process.stdout is not None
        timeout = max(0.0, timeout_at - time.monotonic())
        readable, _, _ = select.select([process.stdout], [], [], timeout)
        if readable:
            line = process.stdout.readline()
            if line:
                return line
    return b""


def _read_exact(process: subprocess.Popen[bytes], length: int, timeout_at: float) -> bytes:
    chunks: list[bytes] = []
    remaining = length
    while remaining > 0 and time.monotonic() < timeout_at:
        assert process.stdout is not None
        chunk = process.stdout.read(remaining)
        if not chunk:
            break
        chunks.append(chunk)
        remaining -= len(chunk)
    data = b"".join(chunks)
    if len(data) != length:
        raise UsageError("RPC message ended early")
    return data


def _process_stderr(process: subprocess.Popen[bytes]) -> str:
    if process.stderr is None:
        return ""
    if process.poll() is None:
        return ""
    try:
        return process.stderr.read().decode("utf-8", errors="replace").strip()
    except Exception:
        return ""


def _terminate_process(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=1)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=1)
