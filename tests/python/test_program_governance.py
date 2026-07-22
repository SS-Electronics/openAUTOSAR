# SPDX-License-Identifier: MIT

from __future__ import annotations

import json
import subprocess
import unittest
from pathlib import Path


class ProgramGovernanceTest(unittest.TestCase):
    def test_program_governance_registers_are_validated(self) -> None:
        repo_root = Path(__file__).resolve().parents[2]
        result = subprocess.run(
            [str(repo_root / "scripts/validate-program-governance.sh")],
            cwd=repo_root,
            stderr=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)

        evidence = repo_root / "out/evidence/program-governance/validation.json"
        data = json.loads(evidence.read_text(encoding="utf-8"))
        self.assertEqual(data["status"], "valid")
        self.assertEqual(data["confirmed_decision_count"], 18)
        self.assertEqual(data["bootstrap_open_question_count"], 12)
        self.assertEqual(data["round4_contract_question_count"], 15)
        self.assertEqual(data["implementation_team_role_count"], 12)
        self.assertEqual(data["coding_rule_count"], 18)
        self.assertEqual(data["backlog_epic_count"], 13)
        self.assertGreaterEqual(data["backlog_item_count"], 90)
        self.assertEqual(data["source_count"], 14)
        self.assertFalse(data["missing"])


if __name__ == "__main__":
    unittest.main()
