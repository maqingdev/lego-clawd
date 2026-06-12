#!/usr/bin/env python3
"""Install global Codex hooks for Lego Clawd status tracking."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
HOOK_SOURCE = PROJECT_ROOT / "tools/codex_status_hook.py"
CODEX_HOME = Path.home() / ".codex"
HOOK_DIR = CODEX_HOME / "hooks"
HOOK_LINK = HOOK_DIR / "codex_status_hook.py"
HOOKS_JSON = CODEX_HOME / "hooks.json"
STATUS_DIR = Path.home() / ".lego-clawd"


def hook_command(state: str) -> str:
    return f"/usr/bin/python3 {HOOK_LINK} {state}"


def hook_entry(state: str) -> dict:
    return {
        "hooks": [
            {
                "type": "command",
                "command": hook_command(state),
                "statusMessage": f"Lego Clawd: {state}",
            }
        ]
    }


HOOKS_TO_INSTALL = {
    "SessionStart": {"state": "idle", "matcher": "startup|resume|clear|compact"},
    "UserPromptSubmit": {"state": "working"},
    "PreToolUse": {"state": "working"},
    "PostToolUse": {"state": "working"},
    "PermissionRequest": {"state": "waiting"},
    "Stop": {"state": "waiting"},
}


def load_hooks() -> dict:
    if not HOOKS_JSON.exists():
        return {"hooks": {}}

    with HOOKS_JSON.open("r", encoding="utf-8") as file:
        data = json.load(file)

    if not isinstance(data, dict):
        raise ValueError(f"{HOOKS_JSON} must contain a JSON object")
    if not isinstance(data.get("hooks"), dict):
        data["hooks"] = {}
    return data


def install_symlink(force: bool) -> None:
    if not HOOK_SOURCE.exists():
        raise FileNotFoundError(HOOK_SOURCE)

    HOOK_DIR.mkdir(parents=True, exist_ok=True)

    if HOOK_LINK.is_symlink() or HOOK_LINK.exists():
        if HOOK_LINK.is_symlink() and HOOK_LINK.resolve() == HOOK_SOURCE:
            return
        if not force:
            raise FileExistsError(
                f"{HOOK_LINK} already exists. Re-run with --force to replace it."
            )
        HOOK_LINK.unlink()

    HOOK_LINK.symlink_to(HOOK_SOURCE)


def install_hooks_config() -> None:
    data = load_hooks()
    hooks = data["hooks"]

    for event, spec in HOOKS_TO_INSTALL.items():
        event_hooks = hooks.setdefault(event, [])
        if not isinstance(event_hooks, list):
            raise ValueError(f"hooks.{event} must be an array")

        command = hook_command(spec["state"])
        already_installed = any(
            isinstance(group, dict)
            and any(
                isinstance(item, dict) and item.get("command") == command
                for item in group.get("hooks", [])
            )
            for group in event_hooks
        )
        if already_installed:
            continue

        entry = hook_entry(spec["state"])
        if "matcher" in spec:
            entry["matcher"] = spec["matcher"]
        event_hooks.append(entry)

    tmp = HOOKS_JSON.with_suffix(".json.tmp")
    tmp.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    tmp.replace(HOOKS_JSON)


def initialize_status_file() -> None:
    STATUS_DIR.mkdir(parents=True, exist_ok=True)
    status_file = STATUS_DIR / "ai-status.json"
    if not status_file.exists():
        status_file.write_text('{"state":"idle","waiting":false}\n', encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--force", action="store_true", help="Replace an existing hook link.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    install_symlink(args.force)
    install_hooks_config()
    initialize_status_file()

    print(f"linked: {HOOK_LINK} -> {HOOK_SOURCE}")
    print(f"updated: {HOOKS_JSON}")
    print(f"status:  {STATUS_DIR / 'ai-status.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
