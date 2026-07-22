# SPDX-License-Identifier: MIT

from __future__ import annotations

import json
import subprocess
import unittest
from pathlib import Path


class CiContractTest(unittest.TestCase):
    def test_jenkins_shared_library_contract_is_valid(self) -> None:
        repo_root = Path(__file__).resolve().parents[2]
        result = subprocess.run(
            [str(repo_root / "scripts/validate-ci-contract.sh")],
            cwd=repo_root,
            stderr=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)

        evidence = repo_root / "out/evidence/ci-contract/validation.json"
        data = json.loads(evidence.read_text(encoding="utf-8"))
        self.assertEqual(data["status"], "valid")
        self.assertEqual(
            data["shared_library"],
            "ci/jenkins/shared/vars/openAutosarPipeline.groovy",
        )
        self.assertFalse(data["missing"])


if __name__ == "__main__":
    unittest.main()
