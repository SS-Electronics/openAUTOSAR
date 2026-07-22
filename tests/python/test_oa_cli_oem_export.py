# SPDX-License-Identifier: MIT

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from oa_cli.model import load_model
from oa_cli.model import write_generated_model
from oa_cli.oem_export import EvidenceExportError
from oa_cli.oem_export import write_evidence_bundle


JSON_MODEL = {
    "service": {
        "name": "UltrasonicDistanceService",
        "instance": "/OpenAUTOSAR/Vehicle/Ultrasonic/FrontCenter",
        "events": [
            {
                "name": "DistanceSample",
                "fields": [{"name": "distance_mm", "type": "uint16"}],
            }
        ],
    },
}


class OemEvidenceExportTest(unittest.TestCase):
    def test_evidence_bundle_is_deterministic_and_traceable(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            model = self._write_model(root)
            generated = root / "generated"
            write_generated_model(load_model(model), generated, "test")
            package = self._write_package(root, generated)
            evidence = self._write_evidence(root)
            test_results = self._write_test_results(root)

            first = root / "export-one"
            second = root / "export-two"
            write_evidence_bundle(
                repo_root=root,
                package_root=package,
                output=first,
                model=model,
                generated=generated,
                evidence_dir=evidence,
                test_results_dir=test_results,
                profile="generic-oem",
                target="qemu-x86_64",
                tool_version="test",
            )
            write_evidence_bundle(
                repo_root=root,
                package_root=package,
                output=second,
                model=model,
                generated=generated,
                evidence_dir=evidence,
                test_results_dir=test_results,
                profile="generic-oem",
                target="qemu-x86_64",
                tool_version="test",
            )

            first_files = sorted(path.relative_to(first) for path in first.rglob("*"))
            second_files = sorted(path.relative_to(second) for path in second.rglob("*"))
            self.assertEqual(first_files, second_files)
            for relative in first_files:
                left = first / relative
                right = second / relative
                if left.is_file():
                    self.assertEqual(left.read_bytes(), right.read_bytes())

            manifest = json.loads((first / "manifest.json").read_text())
            self.assertEqual(manifest["status"], "valid")
            self.assertEqual(manifest["profile"], "generic-oem")
            self.assertEqual(manifest["repository"]["commit"], "abc123")
            self.assertFalse(manifest["missing_optional"])

            inventory = json.loads((first / "artifact-inventory.json").read_text())
            categories = {artifact["category"] for artifact in inventory["artifacts"]}
            self.assertIn("traceability", categories)
            self.assertIn("model", categories)
            self.assertIn("architecture-decision", categories)
            self.assertIn("program-governance", categories)
            self.assertIn("platform-service-doc", categories)
            self.assertIn("implementation-plan", categories)
            self.assertIn("backlog", categories)
            self.assertIn("implementation-workstream", categories)
            self.assertIn("source-baseline", categories)
            self.assertIn("supportability", categories)
            self.assertIn("supplier-delivery", categories)
            self.assertIn("classic-integration", categories)
            self.assertIn("test-results", categories)

            sbom = json.loads((first / "sbom.spdx.json").read_text())
            self.assertEqual(sbom["spdxVersion"], "SPDX-2.3")
            self.assertTrue(sbom["files"])

    def test_missing_required_artifact_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            model = self._write_model(root)
            generated = root / "generated"
            write_generated_model(load_model(model), generated, "test")
            package = self._write_package(root, generated)
            (package / "provenance.json").unlink()

            with self.assertRaises(EvidenceExportError) as context:
                write_evidence_bundle(
                    repo_root=root,
                    package_root=package,
                    output=root / "export",
                    model=model,
                    generated=generated,
                    evidence_dir=root / "missing-evidence",
                    test_results_dir=root / "missing-tests",
                    profile="generic-oem",
                    target="qemu-x86_64",
                    tool_version="test",
                )
            self.assertIn("provenance", str(context.exception))

    def test_optional_evidence_gaps_are_reported(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            model = self._write_model(root)
            generated = root / "generated"
            write_generated_model(load_model(model), generated, "test")
            package = self._write_package(root, generated)

            manifest = write_evidence_bundle(
                repo_root=root,
                package_root=package,
                output=root / "export",
                model=model,
                generated=generated,
                evidence_dir=root / "missing-evidence",
                test_results_dir=root / "missing-tests",
                profile="generic-oem",
                target="qemu-x86_64",
                tool_version="test",
            )
            missing_names = {item["name"] for item in manifest["missing_optional"]}
            self.assertIn("api-abi-validation", missing_names)
            self.assertIn("architecture-decisions-validation", missing_names)
            self.assertIn("architecture-work-products-validation", missing_names)
            self.assertIn("program-governance-validation", missing_names)
            self.assertIn("implementation-plan-validation", missing_names)
            self.assertIn("ci-contract-validation", missing_names)
            self.assertIn("yocto-layer-validation", missing_names)
            self.assertIn("debug-bundle", missing_names)
            self.assertIn("public-header-baseline", missing_names)
            self.assertIn("quality-metrics-validation", missing_names)
            self.assertIn("classic-integration-validation", missing_names)
            self.assertIn("delivery-workflow-validation", missing_names)
            self.assertIn("supplier-delivery-manifest", missing_names)
            self.assertIn("tool-confidence-validation", missing_names)
            self.assertIn("qemu-command", missing_names)
            self.assertIn("network-lab-run", missing_names)

    def _write_model(self, root: Path) -> Path:
        model = root / "model"
        model.mkdir()
        (model / "ultrasonic_service.json").write_text(
            json.dumps(JSON_MODEL, indent=2),
            encoding="utf-8",
        )
        return model

    def _write_package(self, root: Path, generated: Path) -> Path:
        package = root / "package"
        (package / "lib/cmake/openautosar").mkdir(parents=True)
        (package / "lib/cmake/openautosar/OpenAutosarConfig.cmake").write_text(
            "set(OpenAutosar_FOUND TRUE)\n",
            encoding="utf-8",
        )
        (package / "lib").mkdir(exist_ok=True)
        (package / "lib/libopenautosar_core.a").write_bytes(b"archive")
        (package / "include/openautosar/core").mkdir(parents=True)
        (package / "include/openautosar/core/result.h").write_text(
            "// result\n",
            encoding="utf-8",
        )
        (package / "share/openautosar/generated").mkdir(parents=True)
        for path in generated.rglob("*"):
            if path.is_file():
                destination = package / "share/openautosar/generated" / path.relative_to(generated)
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(path.read_bytes())
        classic = package / "share/openautosar/classic-integration"
        classic.mkdir(parents=True)
        (classic / "gateway-config.json").write_text(
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
        (package / "provenance.json").write_text(
            json.dumps({"commit": "abc123"}, indent=2),
            encoding="utf-8",
        )
        (root / "LICENSE").write_text("MIT\n", encoding="utf-8")
        (root / "NOTICE").write_text("openAUTOSAR test notice\n", encoding="utf-8")
        legal = root / "compliance/legal"
        legal.mkdir(parents=True)
        for name in [
            "AUTOSAR-IP-ASSESSMENT.md",
            "BRANDING-GUIDELINES.md",
            "LICENSE-COMPATIBILITY.md",
            "THIRD_PARTY_NOTICES.md",
            "CONTRIBUTOR-IP-POLICY.md",
            "RELEASE-APPROVAL-CHECKLIST.md",
        ]:
            (legal / name).write_text(
                "<!-- SPDX-License-Identifier: MIT -->\n\n# Test\n",
                encoding="utf-8",
            )
        return package

    def _write_evidence(self, root: Path) -> Path:
        evidence = root / "evidence"
        (evidence / "yocto-layer").mkdir(parents=True)
        (evidence / "yocto-layer/validation.json").write_text(
            json.dumps({"status": "valid"}, indent=2),
            encoding="utf-8",
        )
        (evidence / "compliance").mkdir(parents=True)
        (evidence / "compliance/validation.json").write_text(
            json.dumps({"status": "valid"}, indent=2),
            encoding="utf-8",
        )
        (evidence / "ci-contract").mkdir(parents=True)
        (evidence / "ci-contract/validation.json").write_text(
            json.dumps({"status": "valid"}, indent=2),
            encoding="utf-8",
        )
        (evidence / "architecture-work-products").mkdir(parents=True)
        (evidence / "architecture-work-products/validation.json").write_text(
            json.dumps({"status": "valid"}, indent=2),
            encoding="utf-8",
        )
        (evidence / "architecture-decisions").mkdir(parents=True)
        (evidence / "architecture-decisions/validation.json").write_text(
            json.dumps({"status": "valid"}, indent=2),
            encoding="utf-8",
        )
        (evidence / "program-governance").mkdir(parents=True)
        (evidence / "program-governance/validation.json").write_text(
            json.dumps({"status": "valid"}, indent=2),
            encoding="utf-8",
        )
        (evidence / "implementation-plan").mkdir(parents=True)
        (evidence / "implementation-plan/validation.json").write_text(
            json.dumps({"status": "valid"}, indent=2),
            encoding="utf-8",
        )
        (evidence / "tool-confidence").mkdir(parents=True)
        (evidence / "tool-confidence/validation.json").write_text(
            json.dumps({"status": "valid"}, indent=2),
            encoding="utf-8",
        )
        (evidence / "api-abi").mkdir(parents=True)
        (evidence / "api-abi/validation.json").write_text(
            json.dumps({"status": "valid"}, indent=2),
            encoding="utf-8",
        )
        (evidence / "api-abi/public-header-baseline.json").write_text(
            json.dumps({"headers": [], "status": "generated"}, indent=2),
            encoding="utf-8",
        )
        (evidence / "quality-metrics").mkdir(parents=True)
        (evidence / "quality-metrics/validation.json").write_text(
            json.dumps({"status": "valid"}, indent=2),
            encoding="utf-8",
        )
        (evidence / "classic-integration").mkdir(parents=True)
        (evidence / "classic-integration/validation.json").write_text(
            json.dumps({"status": "valid"}, indent=2),
            encoding="utf-8",
        )
        (evidence / "delivery-workflow").mkdir(parents=True)
        (evidence / "delivery-workflow/validation.json").write_text(
            json.dumps({"status": "valid"}, indent=2),
            encoding="utf-8",
        )
        (root / "out/supplier-delivery/generic-oem").mkdir(parents=True)
        (root / "out/supplier-delivery/generic-oem/manifest.json").write_text(
            json.dumps({"status": "valid"}, indent=2),
            encoding="utf-8",
        )
        (evidence / "debug-bundle").mkdir(parents=True)
        (evidence / "debug-bundle/manifest.json").write_text(
            json.dumps({"schema": "openautosar.debug.bundle.v1"}, indent=2),
            encoding="utf-8",
        )
        (evidence / "qemu").mkdir(parents=True)
        (evidence / "qemu/qemu-command.json").write_text(
            json.dumps({"dry_run": True}, indent=2),
            encoding="utf-8",
        )
        (evidence / "network-lab/last-run").mkdir(parents=True)
        (evidence / "network-lab/last-run/run.json").write_text(
            json.dumps({"status": "valid"}, indent=2),
            encoding="utf-8",
        )
        return evidence

    def _write_test_results(self, root: Path) -> Path:
        test_results = root / "test-results"
        (test_results / "qemu-x86_64-dev").mkdir(parents=True)
        (test_results / "qemu-x86_64-dev/ctest.xml").write_text(
            "<testsuite failures=\"0\" />\n",
            encoding="utf-8",
        )
        return test_results


if __name__ == "__main__":
    unittest.main()
