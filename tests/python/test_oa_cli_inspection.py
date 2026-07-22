# SPDX-License-Identifier: MIT

from __future__ import annotations

import io
import json
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

from oa_cli.__main__ import main


class InspectionCliTest(unittest.TestCase):
    def test_status_and_supportability_commands_report_offline_state(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self._write_fixture(root)

            status = self._run_json(root, "status")
            self.assertEqual(status["status"], "valid")
            self.assertEqual(status["machine"], "qemux86-64")
            self.assertEqual(status["service_count"], 1)

            machine = self._run_json(root, "machine", "show")
            self.assertEqual(machine["target"], "qemu-x86_64-agl-unagi")
            self.assertEqual(machine["network"]["tap"], "tap-openautosar0")

            processes = self._run_json(root, "process", "list")
            self.assertGreaterEqual(processes["process_count"], 2)
            names = {item["name"] for item in processes["processes"]}
            self.assertIn("oa-bootstrap", names)
            self.assertIn("oa-dashboard-snapshot", names)

            inspected = self._run_json(root, "process", "inspect", "oa-bootstrap")
            self.assertTrue(inspected["found"])

            groups = self._run_json(root, "function-group", "list")
            self.assertEqual(groups["function_groups"][0]["name"], "MachineFG")
            self.assertIn("DrivingReady", groups["function_groups"][0]["states"])

            state = self._run_json(root, "state", "show")
            self.assertEqual(state["function_groups"][0]["current_state"], "Off")

            services = self._run_json(root, "service", "list")
            self.assertEqual(services["services"][0]["name"], "UltrasonicDistanceService")
            self.assertEqual(services["services"][0]["field_count"], 1)
            self.assertEqual(services["services"][0]["fields"][0]["name"], "CalibrationMode")
            self.assertEqual(services["services"][0]["trigger_count"], 1)
            self.assertEqual(services["services"][0]["triggers"][0]["name"], "ObstacleCleared")

            watched = self._run_json(root, "service", "watch")
            self.assertEqual(watched["mode"], "snapshot")

            health = self._run_json(root, "health", "show")
            self.assertEqual(health["status"], "valid")

            packages = self._run_json(root, "package", "list")
            categories = {item["category"] for item in packages["artifacts"]}
            self.assertIn("binary", categories)
            self.assertIn("policy", categories)
            self.assertIn("architecture-decision", categories)
            self.assertIn("program-governance", categories)
            self.assertIn("platform-service-doc", categories)
            self.assertIn("implementation-plan", categories)
            self.assertIn("backlog", categories)
            self.assertIn("implementation-workstream", categories)
            self.assertIn("source-baseline", categories)
            self.assertIn("classic-integration", categories)

            update = self._run_json(root, "update", "status")
            self.assertEqual(update["activation_state"], "UpdateAllowed")
            self.assertTrue(update["package_provenance"])
            self.assertTrue(update["sbom"])

            diagnostics = self._run_json(root, "diagnostics", "status")
            self.assertEqual(diagnostics["dtc_records_reported"], 1)
            self.assertEqual(diagnostics["uds"]["dtc_records"], 2)

    def test_trace_export_can_write_to_file(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self._write_fixture(root)
            output = root / "trace-export.json"
            result = main([
                "trace",
                "export",
                "--repo-root",
                str(root),
                "--output",
                str(output),
            ])
            self.assertEqual(result, 0)
            data = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(data["status"], "generated")
            self.assertEqual(data["traceability"]["status"], "generated")

    def test_debug_bundle_can_write_redacted_support_snapshot(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self._write_fixture(root)
            output = root / "out/evidence/debug-bundle/manifest.json"
            result = main([
                "debug",
                "bundle",
                "--repo-root",
                str(root),
                "--output",
                str(output),
            ])
            self.assertEqual(result, 0)

            data = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(data["schema"], "openautosar.debug.bundle.v1")
            self.assertEqual(data["status"], "valid")
            self.assertEqual(data["process_state"]["process_count"], 2)
            self.assertEqual(data["service_registry_snapshot"]["service_count"], 1)
            self.assertEqual(data["health_status"]["status"], "valid")
            self.assertEqual(data["recent_state_transitions"]["transition_count"], 1)
            self.assertEqual(
                data["clock_synchronization_status"]["clock_source"],
                "virtual-gptp",
            )

            logs = data["relevant_logs"]["logs"]
            self.assertEqual(len(logs), 1)
            self.assertIn("token=<redacted>", "\n".join(logs[0]["excerpt"]))

            entries = [
                entry["value"]
                for policy in data["redacted_configuration"]["policy_files"]
                for entry in policy["entries"]
            ]
            self.assertIn("<redacted>", entries)

    def _run_json(self, root: Path, *args: str) -> dict:
        stdout = io.StringIO()
        with redirect_stdout(stdout):
            result = main([*args, "--repo-root", str(root)])
        self.assertEqual(result, 0)
        return json.loads(stdout.getvalue())

    def _write_fixture(self, root: Path) -> None:
        self._write_machine(root)
        self._write_generated(root)
        self._write_evidence(root)
        self._write_test_results(root)
        self._write_package(root)
        self._write_policies(root)

    def _write_machine(self, root: Path) -> None:
        path = root / "deployment/machine/qemux86-64-agl-unagi.yaml"
        path.parent.mkdir(parents=True)
        path.write_text(
            "\n".join([
                "target: qemu-x86_64-agl-unagi",
                "status: tier-1-bootstrap",
                "purpose:",
                "  - Adaptive runtime bootstrap",
                "agl:",
                "  release_name: Ultimate Unagi",
                "  release_version: 21.0.2",
                "  branch: unagi",
                "  manifest: unagi_21.0.2.xml",
                "yocto:",
                "  release: scarthgap-5.0.18",
                "machine: qemux86-64",
                "hardware_scope: virtual-only",
                "systemd_boundary:",
                "  starts:",
                "    - openautosar-bootstrap.service",
                "network_policy:",
                "  tap: tap-openautosar0",
                "  can: can0",
                "  firewall_output: nftables",
                "resource_policy:",
                "  user: openautosar",
                "  memory_max: 256M",
                "  tasks_max: 64",
                "  no_new_privileges: true",
            ]) + "\n",
            encoding="utf-8",
        )

    def _write_generated(self, root: Path) -> None:
        generated = root / "out/generated"
        (generated / "traceability").mkdir(parents=True)
        (generated / "manifest-summary.json").write_text(
            json.dumps(
                {
                    "service_count": 1,
                    "services": [
                        {
                            "name": "UltrasonicDistanceService",
                            "instance": "/OpenAUTOSAR/Vehicle/Ultrasonic/FrontCenter",
                            "source": "ultrasonic_service.json",
                            "event_count": 1,
                            "e2e_event_count": 1,
                            "events": [{"name": "DistanceSample"}],
                            "field_count": 1,
                            "fields": [
                                {
                                    "name": "CalibrationMode",
                                    "type": "uint8",
                                    "getter": True,
                                    "setter": True,
                                    "notifier": True,
                                }
                            ],
                            "method_count": 1,
                            "methods": [{"name": "GetDistanceStatistics"}],
                            "trigger_count": 1,
                            "triggers": [{"name": "ObstacleCleared"}],
                        }
                    ],
                    "outputs": [{"name": "traceability"}],
                },
                indent=2,
            ),
            encoding="utf-8",
        )
        (generated / "traceability/traceability.json").write_text(
            json.dumps({"records": [{"element": "service"}], "status": "generated"}, indent=2),
            encoding="utf-8",
        )

    def _write_evidence(self, root: Path) -> None:
        evidence = root / "out/evidence"
        (evidence / "yocto-layer").mkdir(parents=True)
        (evidence / "yocto-layer/validation.json").write_text(
            json.dumps({"status": "valid"}, indent=2),
            encoding="utf-8",
        )
        (evidence / "qemu").mkdir(parents=True)
        (evidence / "qemu/qemu-command.json").write_text(
            json.dumps({"dry_run": True}, indent=2),
            encoding="utf-8",
        )
        (evidence / "network-lab/last-run").mkdir(parents=True)
        (evidence / "network-lab/last-run/run.json").write_text(
            json.dumps({"expected_outcome": "valid"}, indent=2),
            encoding="utf-8",
        )
        (evidence / "network-lab/last-run/gateway-peer.log").write_text(
            "\n".join([
                "diagnostic_reported_dtcs=1 event_memory=2",
                "uds_response_can_id=0x7e8 uds_sid=0x59 "
                "uds_dtc_records=2 diagnostic_event_memory=2",
                "support token=debug-secret",
            ]) + "\n",
            encoding="utf-8",
        )
        (evidence / "classic-integration").mkdir(parents=True)
        (evidence / "classic-integration/validation.json").write_text(
            json.dumps({"status": "valid"}, indent=2),
            encoding="utf-8",
        )

    def _write_test_results(self, root: Path) -> None:
        path = root / "out/test-results/qemu-x86_64-dev/ctest.xml"
        path.parent.mkdir(parents=True)
        path.write_text("<testsuite failures=\"0\" errors=\"0\" />\n", encoding="utf-8")

    def _write_package(self, root: Path) -> None:
        package = root / "out/package/generic-oem"
        (package / "bin").mkdir(parents=True)
        (package / "bin/oa-bootstrap").write_text("#!/bin/sh\n", encoding="utf-8")
        (package / "bin/oa-dashboard-snapshot").write_text("#!/bin/sh\n", encoding="utf-8")
        (package / "lib").mkdir()
        (package / "lib/libopenautosar_core.a").write_bytes(b"archive")
        (package / "include/openautosar/core").mkdir(parents=True)
        (package / "include/openautosar/core/result.h").write_text("// result\n")
        (package / "share/openautosar/security").mkdir(parents=True)
        (package / "share/openautosar/security/openautosar-security-policy.yaml").write_text(
            "schema: openautosar.security.policy.v1\n",
            encoding="utf-8",
        )
        (package / "share/openautosar/classic-integration").mkdir(parents=True)
        (package / "share/openautosar/classic-integration/gateway-config.json").write_text(
            json.dumps({"schema": "openautosar.classic-gateway.config.v1"}, indent=2),
            encoding="utf-8",
        )
        decisions = package / "share/openautosar/work-products/architecture-decisions"
        decisions.mkdir(parents=True)
        (decisions / "ADR-0001-monorepo-structure.md").write_text(
            "<!-- SPDX-License-Identifier: MIT -->\n\n# ADR-0001: Monorepo Structure\n",
            encoding="utf-8",
        )
        governance = package / "share/openautosar/work-products/governance"
        governance.mkdir(parents=True)
        (governance / "program-governance.yaml").write_text(
            "# SPDX-License-Identifier: MIT\nschema: openautosar.program-governance.v1\n",
            encoding="utf-8",
        )
        services = package / "share/openautosar/work-products/platform-services"
        services.mkdir(parents=True)
        (services / "PLATFORM_SERVICE_INDEX.md").write_text(
            "<!-- SPDX-License-Identifier: MIT -->\n\n# Platform Service Index\n",
            encoding="utf-8",
        )
        implementation = package / "share/openautosar/work-products/implementation"
        implementation.mkdir(parents=True)
        (implementation / "PROGRAM-PLAN.md").write_text(
            "<!-- SPDX-License-Identifier: MIT -->\n\n# OpenAUTOSAR Program Plan\n",
            encoding="utf-8",
        )
        backlog = package / "share/openautosar/work-products/backlog"
        backlog.mkdir(parents=True)
        (backlog / "initial-backlog.yaml").write_text(
            "# SPDX-License-Identifier: MIT\nschema: openautosar.initial-backlog.v1\n",
            encoding="utf-8",
        )
        planning = package / "share/openautosar/work-products/planning"
        planning.mkdir(parents=True)
        (planning / "implementation-workstreams.yaml").write_text(
            "# SPDX-License-Identifier: MIT\n"
            "schema: openautosar.implementation-workstreams.v1\n",
            encoding="utf-8",
        )
        sources = package / "share/openautosar/work-products/requirements/autosar"
        sources.mkdir(parents=True)
        (sources / "source-baseline.yaml").write_text(
            "# SPDX-License-Identifier: MIT\n"
            "schema: openautosar.official-source-baseline.v1\n",
            encoding="utf-8",
        )
        (package / "share/openautosar/oem-export").mkdir(parents=True)
        (package / "share/openautosar/oem-export/sbom.spdx.json").write_text(
            json.dumps({"spdxVersion": "SPDX-2.3"}, indent=2),
            encoding="utf-8",
        )
        (package / "provenance.json").write_text(
            json.dumps({"commit": "abc123"}, indent=2),
            encoding="utf-8",
        )

    def _write_policies(self, root: Path) -> None:
        state_policy = root / (
            "meta-openautosar/recipes-platform/openautosar-state-management/files/"
            "openautosar-state-management-policy.yaml"
        )
        state_policy.parent.mkdir(parents=True)
        state_policy.write_text(
            "\n".join([
                "function_groups:",
                "  - name: MachineFG",
                "    initial_state: Off",
                "    states:",
                "      - Off",
                "      - Startup",
                "      - DrivingReady",
                "    transitions:",
                "      - from: Off",
                "        to: Startup",
            ]) + "\n",
            encoding="utf-8",
        )
        ucm_policy = root / (
            "meta-openautosar/recipes-platform/openautosar-vehicle-ucm/files/"
            "openautosar-vehicle-ucm-policy.yaml"
        )
        ucm_policy.parent.mkdir(parents=True)
        ucm_policy.write_text(
            "\n".join([
                "mode: coordinated-campaign-simulator",
                "target_scope: qemu-vehicle-fleet",
                "activation_state: UpdateAllowed",
                "trusted_signers:",
                "  - openautosar-dev",
                "rollback:",
                "  coordinated_rollback_on_health_failure: true",
                "  mark_failed_ecu_recovery_required: true",
            ]) + "\n",
            encoding="utf-8",
        )
        service = root / (
            "meta-openautosar/recipes-platform/openautosar-runtime/files/"
            "openautosar-bootstrap.service"
        )
        service.parent.mkdir(parents=True)
        service.write_text("[Service]\nExecStart=/usr/bin/oa-bootstrap\n", encoding="utf-8")
        process_policy = root / (
            "meta-openautosar/recipes-platform/openautosar-process-launcher/files/"
            "openautosar-process-launcher-policy.yaml"
        )
        process_policy.parent.mkdir(parents=True)
        process_policy.write_text(
            "\n".join([
                "cgroup_v2:",
                "  cpu_weight: 100",
                "  file_descriptor_limit: 1024",
                "  memory_max_bytes: 67108864",
                "capabilities:",
                "  no_new_privileges: true",
            ]) + "\n",
            encoding="utf-8",
        )
        time_policy = root / (
            "meta-openautosar/recipes-platform/openautosar-time-network/files/"
            "openautosar-time-network-policy.yaml"
        )
        time_policy.parent.mkdir(parents=True)
        time_policy.write_text(
            "\n".join([
                "time:",
                "  virtual_clock:",
                "    enabled: true",
                "    source: virtual-gptp",
                "  domains:",
                "    - id: gptp",
                "      max_uncertainty_ns: 1000000",
                "      loss_of_sync_timeout_ns: 1000000000",
                "      jump_policy: degrade",
            ]) + "\n",
            encoding="utf-8",
        )
        crypto_policy = root / (
            "meta-openautosar/recipes-security/openautosar-crypto/files/"
            "openautosar-crypto-policy.yaml"
        )
        crypto_policy.parent.mkdir(parents=True)
        crypto_policy.write_text(
            "\n".join([
                "key_slots:",
                "  - key_slot: slot://dev-signing",
                "    algorithm: hmac-sha256",
            ]) + "\n",
            encoding="utf-8",
        )


if __name__ == "__main__":
    unittest.main()
