# SPDX-License-Identifier: MIT

from __future__ import annotations

import json
import subprocess
import unittest
from pathlib import Path


class QualityMetricsTest(unittest.TestCase):
    def test_quality_metrics_faults_and_budgets_are_validated(self) -> None:
        repo_root = Path(__file__).resolve().parents[2]
        result = subprocess.run(
            [str(repo_root / "scripts/validate-quality-metrics.sh")],
            cwd=repo_root,
            stderr=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)

        evidence = repo_root / "out/evidence/quality-metrics/validation.json"
        data = json.loads(evidence.read_text(encoding="utf-8"))
        self.assertEqual(data["status"], "valid")
        self.assertEqual(data["required_metric_count"], 13)
        self.assertEqual(data["required_fault_count"], 16)
        self.assertGreaterEqual(data["budget_component_count"], 3)
        self.assertEqual(data["required_budget_field_count"], 6)
        self.assertFalse(data["missing"])


if __name__ == "__main__":
    unittest.main()
