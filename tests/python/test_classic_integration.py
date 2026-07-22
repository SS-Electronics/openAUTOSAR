# SPDX-License-Identifier: MIT

from __future__ import annotations

import json
import subprocess
import unittest
from pathlib import Path


class ClassicIntegrationTest(unittest.TestCase):
    def test_classic_integration_model_generates_runtime_config(self) -> None:
        repo_root = Path(__file__).resolve().parents[2]
        result = subprocess.run(
            [str(repo_root / "scripts/validate-classic-integration.sh")],
            cwd=repo_root,
            stderr=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)

        evidence = repo_root / "out/evidence/classic-integration/validation.json"
        data = json.loads(evidence.read_text(encoding="utf-8"))
        self.assertEqual(data["status"], "valid")
        self.assertEqual(data["required_external_system_count"], 6)
        self.assertEqual(data["required_route_type_count"], 8)
        self.assertEqual(data["route_count"], 8)
        self.assertEqual(data["signal_count"], 7)
        self.assertTrue(data["software_in_loop"])
        self.assertTrue(data["socketcan_vcan"])
        self.assertFalse(data["missing"])

        generated = repo_root / "out/generated/classic-integration"
        runtime = (generated / "gateway-runtime.conf").read_text(encoding="utf-8")
        self.assertIn("schema=openautosar.classic-gateway.runtime.v1", runtime)
        self.assertIn("ultrasonic_can_id=0x321", runtime)
        self.assertIn("event_name=DistanceSample", runtime)

        route_plan = json.loads((generated / "gateway-route-plan.json").read_text())
        route_types = {route["type"] for route in route_plan["routes"]}
        self.assertIn("service-to-signal mapping for control/status tests", route_types)
        self.assertIn("SOME/IP-to-CAN/CAN-FD-style mapping", route_types)
        self.assertIn("Function Group state", route_types)


if __name__ == "__main__":
    unittest.main()
