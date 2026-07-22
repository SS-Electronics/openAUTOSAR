# SPDX-License-Identifier: MIT

from __future__ import annotations

import json
import subprocess
import unittest
from pathlib import Path


class ArchitectureWorkProductsTest(unittest.TestCase):
    def test_architecture_work_products_are_validated(self) -> None:
        repo_root = Path(__file__).resolve().parents[2]
        result = subprocess.run(
            [str(repo_root / "scripts/validate-architecture-work-products.sh")],
            cwd=repo_root,
            stderr=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)

        evidence = repo_root / "out/evidence/architecture-work-products/validation.json"
        data = json.loads(evidence.read_text(encoding="utf-8"))
        self.assertEqual(data["status"], "valid")
        self.assertEqual(data["required_file_count"], 28)
        self.assertEqual(data["required_threat_surface_count"], 15)
        self.assertFalse(data["missing"])


if __name__ == "__main__":
    unittest.main()
