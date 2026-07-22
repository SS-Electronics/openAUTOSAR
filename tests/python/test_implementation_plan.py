# SPDX-License-Identifier: MIT

from __future__ import annotations

import json
import subprocess
import unittest
from pathlib import Path


class ImplementationPlanTest(unittest.TestCase):
    def test_implementation_plan_is_validated(self) -> None:
        repo_root = Path(__file__).resolve().parents[2]
        result = subprocess.run(
            [str(repo_root / "scripts/validate-implementation-plan.sh")],
            cwd=repo_root,
            stderr=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)

        evidence = repo_root / "out/evidence/implementation-plan/validation.json"
        data = json.loads(evidence.read_text(encoding="utf-8"))
        self.assertEqual(data["status"], "valid")
        self.assertEqual(data["vertical_slice_step_count"], 12)
        self.assertEqual(data["workstream_count"], 9)
        self.assertEqual(data["milestone_count"], 3)
        self.assertEqual(data["parallelization_track_count"], 8)
        self.assertFalse(data["missing"])


if __name__ == "__main__":
    unittest.main()
