# SPDX-License-Identifier: MIT

from __future__ import annotations

import json
import subprocess
import unittest
from pathlib import Path


class ToolConfidenceTest(unittest.TestCase):
    def test_tool_confidence_manifest_is_validated(self) -> None:
        repo_root = Path(__file__).resolve().parents[2]
        result = subprocess.run(
            [str(repo_root / "scripts/validate-tool-confidence.sh")],
            cwd=repo_root,
            stderr=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)

        evidence = repo_root / "out/evidence/tool-confidence/validation.json"
        data = json.loads(evidence.read_text(encoding="utf-8"))
        self.assertEqual(data["status"], "valid")
        self.assertEqual(data["required_class_count"], 6)
        self.assertEqual(data["generator_record_count"], 8)
        self.assertFalse(data["missing"])


if __name__ == "__main__":
    unittest.main()
