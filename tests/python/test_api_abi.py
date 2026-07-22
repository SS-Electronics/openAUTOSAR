# SPDX-License-Identifier: MIT

from __future__ import annotations

import json
import subprocess
import unittest
from pathlib import Path


class ApiAbiTest(unittest.TestCase):
    def test_api_abi_policy_and_public_headers_are_validated(self) -> None:
        repo_root = Path(__file__).resolve().parents[2]
        result = subprocess.run(
            [str(repo_root / "scripts/validate-api-abi.sh")],
            cwd=repo_root,
            stderr=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)

        validation = repo_root / "out/evidence/api-abi/validation.json"
        data = json.loads(validation.read_text(encoding="utf-8"))
        self.assertEqual(data["status"], "valid")
        self.assertGreaterEqual(data["public_header_count"], data["minimum_public_headers"])
        self.assertFalse(data["missing"])

        baseline = repo_root / "out/evidence/api-abi/public-header-baseline.json"
        headers = json.loads(baseline.read_text(encoding="utf-8"))["headers"]
        self.assertEqual(len(headers), data["public_header_count"])
        self.assertTrue(all(item["sha256"] for item in headers))


if __name__ == "__main__":
    unittest.main()
