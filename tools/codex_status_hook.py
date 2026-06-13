#!/usr/bin/env python3
"""Write the current Codex activity state for Lego Clawd.

Install this script under ~/.codex/hooks/ or call it from ~/.codex/hooks.json.
"""

from __future__ import annotations

import json
import sys
from datetime import datetime
from pathlib import Path
from typing import Any


def normalize_state(value: str) -> str:
    text = value.strip().lower()
    if text in {"idle", "ready"}:
        return "idle"
    if text in {"working", "running", "thinking"}:
        return "working"
    if text in {"pending", "approval", "waiting_approval", "asking_approval"}:
        return "pending"
    if text in {"waiting", "waiting_input", "done"}:
        return "waiting"
    return "idle"


def read_hook_input() -> dict[str, Any]:
    if sys.stdin.isatty():
        return {}

    try:
        raw = sys.stdin.read()
    except OSError:
        return {}

    if not raw.strip():
        return {}

    try:
        data = json.loads(raw)
    except json.JSONDecodeError:
        return {}

    return data if isinstance(data, dict) else {}


def main() -> int:
    state = normalize_state(sys.argv[1] if len(sys.argv) > 1 else "idle")
    hook_input = read_hook_input()
    base = Path.home() / ".lego-clawd"
    base.mkdir(parents=True, exist_ok=True)

    payload = {
        "state": state,
        "waiting": state == "waiting",
        "pending": state == "pending",
        "updatedAt": datetime.now().astimezone().isoformat(timespec="seconds"),
        "hookEvent": hook_input.get("hook_event_name"),
        "sessionId": hook_input.get("session_id"),
        "turnId": hook_input.get("turn_id"),
        "cwd": hook_input.get("cwd"),
        "model": hook_input.get("model"),
    }

    status_file = base / "ai-status.json"
    tmp_file = base / "ai-status.json.tmp"
    tmp_file.write_text(json.dumps(payload, separators=(",", ":")) + "\n", encoding="utf-8")
    tmp_file.replace(status_file)

    events_file = base / "hook-events.jsonl"
    with events_file.open("a", encoding="utf-8") as file:
        file.write(json.dumps(payload, separators=(",", ":")) + "\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
