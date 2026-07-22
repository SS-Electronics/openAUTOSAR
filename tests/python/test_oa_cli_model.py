# SPDX-License-Identifier: MIT

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from oa_cli.model import ModelError
from oa_cli.model import load_model
from oa_cli.model import validation_summary
from oa_cli.model import write_generated_model


JSON_MODEL = {
    "schema": "openautosar.example.ultrasonic.v1",
    "service": {
        "name": "UltrasonicDistanceService",
        "instance": "/OpenAUTOSAR/Vehicle/Ultrasonic/FrontCenter",
        "events": [
            {
                "name": "DistanceSample",
                "e2e": {
                    "profile": "profile01",
                    "data_id": 0xA5018001,
                    "counter_bits": 8,
                    "max_delta_counter": 1,
                    "timeout_ms": 100,
                    "max_repetitions": 0,
                },
                "fields": [
                    {"name": "distance_mm", "type": "uint16"},
                    {"name": "alive_counter", "type": "uint8"},
                    {"name": "valid", "type": "bool"},
                ],
            }
        ],
    },
}


ARXML_MODEL = """<?xml version="1.0" encoding="UTF-8"?>
<AUTOSAR>
  <AR-PACKAGES>
    <AR-PACKAGE>
      <SHORT-NAME>OpenAutosarExamples</SHORT-NAME>
      <ELEMENTS>
        <SERVICE-INTERFACE>
          <SHORT-NAME>ParkingAssistService</SHORT-NAME>
          <INSTANCE-SPECIFIER>/OpenAUTOSAR/Vehicle/Parking/Front</INSTANCE-SPECIFIER>
          <EVENTS>
            <EVENT>
              <SHORT-NAME>DistanceSample</SHORT-NAME>
              <FIELDS>
                <FIELD>
                  <SHORT-NAME>distance_mm</SHORT-NAME>
                  <TYPE>uint16</TYPE>
                </FIELD>
                <FIELD>
                  <SHORT-NAME>confidence</SHORT-NAME>
                  <TYPE-TREF>/OpenAUTOSAR/Types/uint8</TYPE-TREF>
                </FIELD>
              </FIELDS>
            </EVENT>
          </EVENTS>
        </SERVICE-INTERFACE>
      </ELEMENTS>
    </AR-PACKAGE>
  </AR-PACKAGES>
</AUTOSAR>
"""


class ModelToolingTest(unittest.TestCase):
    def test_json_model_loads_into_typed_ir(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            model_dir = Path(temp) / "model"
            model_dir.mkdir()
            (model_dir / "ultrasonic_service.json").write_text(
                json.dumps(JSON_MODEL, indent=2),
                encoding="utf-8",
            )

            bundle = load_model(model_dir)
            self.assertEqual(len(bundle.services), 1)
            self.assertEqual(bundle.services[0].name, "UltrasonicDistanceService")
            self.assertEqual(bundle.services[0].events[0].fields[0].type_name, "uint16")
            e2e = bundle.services[0].events[0].e2e
            self.assertIsNotNone(e2e)
            self.assertEqual(e2e.data_id if e2e is not None else 0, 0xA5018001)

            summary = validation_summary(bundle)
            self.assertEqual(summary["status"], "valid")
            self.assertEqual(summary["service_count"], 1)
            self.assertEqual(summary["services"][0]["e2e_event_count"], 1)

    def test_arxml_model_loads_into_typed_ir(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            model_dir = Path(temp) / "model"
            model_dir.mkdir()
            (model_dir / "parking_service.arxml").write_text(ARXML_MODEL, encoding="utf-8")

            bundle = load_model(model_dir)
            service = bundle.services[0]
            self.assertEqual(service.name, "ParkingAssistService")
            self.assertEqual(service.instance, "/OpenAUTOSAR/Vehicle/Parking/Front")
            self.assertEqual(service.events[0].fields[1].type_name, "uint8")

    def test_generation_is_deterministic_and_traceable(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            model_dir = root / "model"
            model_dir.mkdir()
            (model_dir / "ultrasonic_service.json").write_text(
                json.dumps(JSON_MODEL, indent=2),
                encoding="utf-8",
            )
            bundle = load_model(model_dir)

            output_one = root / "generated-one"
            output_two = root / "generated-two"
            write_generated_model(bundle, output_one, "test")
            write_generated_model(bundle, output_two, "test")

            first_files = sorted(path.relative_to(output_one) for path in output_one.rglob("*"))
            second_files = sorted(path.relative_to(output_two) for path in output_two.rglob("*"))
            self.assertEqual(first_files, second_files)
            for relative in first_files:
                first = output_one / relative
                second = output_two / relative
                if first.is_file():
                    self.assertEqual(first.read_bytes(), second.read_bytes())

            summary = json.loads((output_one / "manifest-summary.json").read_text())
            self.assertEqual(summary["services"][0]["name"], "UltrasonicDistanceService")
            header = output_one / "include/openautosar/generated/ultrasonic_distance_service.h"
            header_text = header.read_text()
            self.assertIn("Generated by oa-cli; do not edit manually.", header_text)
            self.assertIn("struct DistanceSample final", header_text)
            self.assertIn("kUltrasonicDistanceServiceDistanceSampleE2EDataId", header_text)
            service_manifest = json.loads(
                (output_one / "services/ultrasonic_distance_service.json").read_text()
            )
            self.assertEqual(service_manifest["events"][0]["e2e"]["profile"], "profile01")
            traceability = json.loads(
                (output_one / "traceability/traceability.json").read_text()
            )
            self.assertTrue(
                any(record["element"].endswith(".distance_mm")
                    for record in traceability["records"])
            )
            self.assertTrue(
                any(record["element"] == "e2e:UltrasonicDistanceService.DistanceSample"
                    for record in traceability["records"])
            )

    def test_semantic_validation_reports_model_source(self) -> None:
        invalid_model = {
            "service": {
                "name": "InvalidService",
                "instance": "OpenAUTOSAR/Vehicle/Ultrasonic",
                "events": [
                    {
                        "name": "DistanceSample",
                        "fields": [{"name": "distance_mm", "type": "uint128"}],
                    }
                ],
            }
        }

        with tempfile.TemporaryDirectory() as temp:
            model_dir = Path(temp) / "model"
            model_dir.mkdir()
            path = model_dir / "invalid_service.json"
            path.write_text(json.dumps(invalid_model, indent=2), encoding="utf-8")

            with self.assertRaises(ModelError) as context:
                load_model(model_dir)
            self.assertIn("invalid_service.json", str(context.exception))
            self.assertIn("service instance must start with /", str(context.exception))

    def test_e2e_validation_reports_model_source(self) -> None:
        invalid_model = {
            "service": {
                "name": "InvalidE2EService",
                "instance": "/OpenAUTOSAR/Vehicle/Ultrasonic",
                "events": [
                    {
                        "name": "DistanceSample",
                        "e2e": {"profile": "unknown", "data_id": 1},
                        "fields": [{"name": "distance_mm", "type": "uint16"}],
                    }
                ],
            }
        }

        with tempfile.TemporaryDirectory() as temp:
            model_dir = Path(temp) / "model"
            model_dir.mkdir()
            path = model_dir / "invalid_e2e.json"
            path.write_text(json.dumps(invalid_model, indent=2), encoding="utf-8")

            with self.assertRaises(ModelError) as context:
                load_model(model_dir)
            self.assertIn("invalid_e2e.json", str(context.exception))
            self.assertIn("unsupported E2E profile", str(context.exception))


if __name__ == "__main__":
    unittest.main()
