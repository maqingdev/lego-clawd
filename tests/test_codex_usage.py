import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import codex_usage


class NormalizeWindowsTests(unittest.TestCase):
    def test_classifies_standard_dual_windows_by_duration(self) -> None:
        usage = codex_usage.normalize_windows(
            {"usedPercent": 12, "windowDurationMins": 300, "resetsAt": 1_800_000_000},
            {"usedPercent": 34, "windowDurationMins": 10_080, "resetsAt": 1_800_000_100},
        )

        self.assertEqual(usage["fiveHour"]["remainingPercent"], 88)
        self.assertEqual(usage["weekly"]["remainingPercent"], 66)

    def test_classifies_weekly_only_primary_window(self) -> None:
        usage = codex_usage.normalize_windows(
            {"usedPercent": 5, "windowDurationMins": 10_080, "resetsAt": 1_784_518_236},
            None,
        )

        self.assertIsNone(usage["fiveHour"])
        self.assertEqual(usage["weekly"]["remainingPercent"], 95)
        self.assertEqual(usage["weekly"]["durationMinutes"], 10_080)

    def test_supports_auth_endpoint_duration_seconds(self) -> None:
        usage = codex_usage.normalize_windows(
            {"used_percent": 20, "limit_window_seconds": 604_800},
            None,
        )

        self.assertIsNone(usage["fiveHour"])
        self.assertEqual(usage["weekly"]["remainingPercent"], 80)

    def test_falls_back_to_legacy_positions_without_duration(self) -> None:
        usage = codex_usage.normalize_windows(
            {"usedPercent": 10},
            {"usedPercent": 25},
        )

        self.assertEqual(usage["fiveHour"]["remainingPercent"], 90)
        self.assertEqual(usage["weekly"]["remainingPercent"], 75)


if __name__ == "__main__":
    unittest.main()
