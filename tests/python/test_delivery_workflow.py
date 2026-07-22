# SPDX-License-Identifier: MIT

from __future__ import annotations

import json
import subprocess
import tempfile
import unittest
from pathlib import Path

from oa_cli.delivery import write_supplier_delivery_bundle
from oa_cli.model import load_model
from oa_cli.model import write_generated_model


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


class DeliveryWorkflowTest(unittest.TestCase):
    def test_delivery_workflow_profiles_are_validated(self) -> None:
        repo_root = Path(__file__).resolve().parents[2]
        result = subprocess.run(
            [str(repo_root / "scripts/validate-delivery-workflow.sh"), "--source-only"],
            cwd=repo_root,
            stderr=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)

        evidence = repo_root / "out/evidence/delivery-workflow/validation.json"
        data = json.loads(evidence.read_text(encoding="utf-8"))
        self.assertEqual(data["status"], "valid")
        self.assertEqual(data["required_input_count"], 14)
        self.assertEqual(data["import_control_count"], 10)
        self.assertEqual(data["freeze_item_count"], 10)
        self.assertEqual(data["delivery_section_count"], 9)
        self.assertFalse(data["missing"])

    def test_supplier_delivery_bundle_is_deterministic(self) -> None:
        repo_root = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            model = self._write_model(root)
            generated = root / "generated"
            write_generated_model(load_model(model), generated, "test")
            package = self._write_package(root, generated)
            evidence = self._write_evidence(root)
            test_results = self._write_test_results(root)
            oem_export = self._write_oem_export(root)

            first = root / "delivery-one"
            second = root / "delivery-two"
            write_supplier_delivery_bundle(
                repo_root=repo_root,
                package_root=package,
                output=first,
                model=model,
                generated=generated,
                evidence_dir=evidence,
                test_results_dir=test_results,
                oem_export=oem_export,
                profile="generic-supplier",
                target="qemu-x86_64",
                tool_version="test",
            )
            write_supplier_delivery_bundle(
                repo_root=repo_root,
                package_root=package,
                output=second,
                model=model,
                generated=generated,
                evidence_dir=evidence,
                test_results_dir=test_results,
                oem_export=oem_export,
                profile="generic-supplier",
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
            self.assertEqual(manifest["schema"], "openautosar.supplier-delivery.manifest.v1")
            self.assertEqual(manifest["status"], "valid")
            self.assertEqual(manifest["profile"], "generic-supplier")
            self.assertEqual(manifest["section_count"], 9)

            validation = json.loads((first / "validation-summary.json").read_text())
            self.assertEqual(validation["status"], "valid")
            self.assertFalse(validation["missing"])
            self.assertTrue(
                (first / "00-cover/delivery-note.pdf").read_bytes().startswith(b"%PDF-1.4")
            )
            self.assertTrue((first / "40-build/hashes.txt").read_text(encoding="utf-8"))

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
        (package / "bin").mkdir(parents=True)
        (package / "bin/oa-test").write_text("#!/bin/sh\n", encoding="utf-8")
        (package / "lib").mkdir()
        (package / "lib/libopenautosar_core.a").write_bytes(b"archive")
        (package / "share/openautosar/generated").mkdir(parents=True)
        for path in generated.rglob("*"):
            if path.is_file():
                destination = package / "share/openautosar/generated" / path.relative_to(generated)
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(path.read_bytes())
        (package / "provenance.json").write_text(
            json.dumps({"commit": "abc123"}, indent=2),
            encoding="utf-8",
        )
        return package

    def _write_evidence(self, root: Path) -> Path:
        evidence = root / "evidence"
        (evidence / "quality-metrics").mkdir(parents=True)
        (evidence / "quality-metrics/validation.json").write_text(
            json.dumps({"status": "valid"}, indent=2),
            encoding="utf-8",
        )
        return evidence

    def _write_test_results(self, root: Path) -> Path:
        test_results = root / "test-results/qemu-x86_64-dev"
        test_results.mkdir(parents=True)
        (test_results / "ctest.xml").write_text(
            "<testsuite failures=\"0\" />\n",
            encoding="utf-8",
        )
        return root / "test-results"

    def _write_oem_export(self, root: Path) -> Path:
        oem_export = root / "oem-export"
        oem_export.mkdir()
        (oem_export / "sbom.spdx.json").write_text(
            json.dumps({"spdxVersion": "SPDX-2.3", "files": []}, indent=2),
            encoding="utf-8",
        )
        return oem_export


if __name__ == "__main__":
    unittest.main()
