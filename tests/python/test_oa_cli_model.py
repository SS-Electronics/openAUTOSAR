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
        "fields": [
            {
                "name": "CalibrationMode",
                "type": "uint8",
                "getter": True,
                "setter": True,
                "notifier": True,
            }
        ],
        "triggers": [{"name": "ObstacleCleared"}],
        "methods": [
            {
                "name": "GetDistanceStatistics",
                "request": [{"name": "window_ms", "type": "uint16"}],
                "response": [
                    {"name": "minimum_distance_mm", "type": "uint16"},
                    {"name": "maximum_distance_mm", "type": "uint16"},
                    {"name": "sample_count", "type": "uint32"},
                ],
                "errors": [
                    {
                        "name": "WindowUnavailable",
                        "code": 1,
                        "description": "The requested statistics window is not available",
                    },
                    {
                        "name": "SensorDegraded",
                        "code": 2,
                        "description": (
                            "Statistics are unavailable because sensor quality is degraded"
                        ),
                    },
                ],
            },
            {
                "name": "ResetCalibration",
                "fire_and_forget": True,
                "request": [{"name": "calibration_slot", "type": "uint8"}],
            },
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
            self.assertEqual(bundle.services[0].methods[0].name, "GetDistanceStatistics")
            self.assertEqual(bundle.services[0].methods[0].request_fields[0].type_name, "uint16")
            self.assertEqual(
                bundle.services[0].methods[0].response_fields[2].type_name,
                "uint32",
            )
            self.assertEqual(bundle.services[0].methods[0].errors[0].name, "WindowUnavailable")
            self.assertEqual(bundle.services[0].methods[0].errors[0].code, 1)
            self.assertEqual(bundle.services[0].methods[1].name, "ResetCalibration")
            self.assertTrue(bundle.services[0].methods[1].fire_and_forget)
            self.assertEqual(bundle.services[0].methods[1].request_fields[0].type_name, "uint8")
            self.assertEqual(bundle.services[0].methods[1].response_fields, ())
            self.assertEqual(bundle.services[0].service_fields[0].name, "CalibrationMode")
            self.assertEqual(bundle.services[0].service_fields[0].type_name, "uint8")
            self.assertTrue(bundle.services[0].service_fields[0].getter)
            self.assertTrue(bundle.services[0].service_fields[0].setter)
            self.assertTrue(bundle.services[0].service_fields[0].notifier)
            self.assertEqual(bundle.services[0].triggers[0].name, "ObstacleCleared")
            e2e = bundle.services[0].events[0].e2e
            self.assertIsNotNone(e2e)
            self.assertEqual(e2e.data_id if e2e is not None else 0, 0xA5018001)

            summary = validation_summary(bundle)
            self.assertEqual(summary["status"], "valid")
            self.assertEqual(summary["service_count"], 1)
            self.assertEqual(summary["services"][0]["e2e_event_count"], 1)
            self.assertEqual(summary["services"][0]["method_count"], 2)
            self.assertEqual(summary["services"][0]["methods"][0]["error_count"], 2)
            self.assertEqual(
                summary["services"][0]["methods"][0]["errors"][0]["name"],
                "WindowUnavailable",
            )
            self.assertFalse(summary["services"][0]["methods"][0]["fire_and_forget"])
            self.assertTrue(summary["services"][0]["methods"][1]["fire_and_forget"])
            self.assertEqual(summary["services"][0]["field_count"], 1)
            self.assertEqual(summary["services"][0]["fields"][0]["name"], "CalibrationMode")
            self.assertTrue(summary["services"][0]["fields"][0]["getter"])
            self.assertTrue(summary["services"][0]["fields"][0]["setter"])
            self.assertTrue(summary["services"][0]["fields"][0]["notifier"])
            self.assertEqual(summary["services"][0]["trigger_count"], 1)
            self.assertEqual(summary["services"][0]["triggers"][0]["name"], "ObstacleCleared")

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
            self.assertIn("struct GetDistanceStatisticsRequest final", header_text)
            self.assertIn("struct GetDistanceStatisticsResponse final", header_text)
            self.assertIn("enum class GetDistanceStatisticsError", header_text)
            self.assertIn("WindowUnavailable = 1U", header_text)
            self.assertIn("SensorDegraded = 2U", header_text)
            self.assertIn("struct ResetCalibrationRequest final", header_text)
            self.assertNotIn("struct ResetCalibrationResponse final", header_text)
            self.assertIn("struct CalibrationModeField final", header_text)
            self.assertIn("class UltrasonicDistanceServiceProxy final", header_text)
            self.assertIn("class UltrasonicDistanceServiceSkeleton final", header_text)
            self.assertIn("SubscribeDistanceSample", header_text)
            self.assertIn("PublishDistanceSample", header_text)
            self.assertIn("SubscribeObstacleClearedTrigger", header_text)
            self.assertIn("PollObstacleClearedTrigger", header_text)
            self.assertIn("FireObstacleClearedTrigger", header_text)
            self.assertIn("GetCalibrationMode", header_text)
            self.assertIn("SetCalibrationMode", header_text)
            self.assertIn("SubscribeCalibrationModeField", header_text)
            self.assertIn("PollCalibrationModeField", header_text)
            self.assertIn("UpdateCalibrationMode", header_text)
            self.assertIn("CallGetDistanceStatistics", header_text)
            self.assertIn("CallGetDistanceStatisticsFuture", header_text)
            self.assertIn("TakeGetDistanceStatisticsResult", header_text)
            self.assertIn("FireAndForgetResetCalibration", header_text)
            self.assertIn("TakeResetCalibrationCall", header_text)
            self.assertIn("CompleteGetDistanceStatistics", header_text)
            self.assertIn("CompleteGetDistanceStatisticsError", header_text)
            self.assertIn("kUltrasonicDistanceServiceGetDistanceStatisticsErrorDomain", header_text)
            self.assertNotIn("FireAndForgetResetCalibrationFuture", header_text)
            self.assertNotIn("TakeResetCalibrationResult", header_text)
            self.assertNotIn("CompleteResetCalibration", header_text)
            self.assertIn("kUltrasonicDistanceServiceDistanceSampleE2EDataId", header_text)
            service_manifest = json.loads(
                (output_one / "services/ultrasonic_distance_service.json").read_text()
            )
            self.assertEqual(service_manifest["events"][0]["e2e"]["profile"], "profile01")
            self.assertEqual(service_manifest["fields"][0]["name"], "CalibrationMode")
            self.assertEqual(service_manifest["fields"][0]["type"], "uint8")
            self.assertTrue(service_manifest["fields"][0]["getter"])
            self.assertTrue(service_manifest["fields"][0]["setter"])
            self.assertTrue(service_manifest["fields"][0]["notifier"])
            self.assertEqual(service_manifest["triggers"][0]["name"], "ObstacleCleared")
            self.assertEqual(service_manifest["methods"][0]["name"], "GetDistanceStatistics")
            self.assertEqual(
                service_manifest["methods"][0]["errors"][0]["name"],
                "WindowUnavailable",
            )
            self.assertEqual(service_manifest["methods"][0]["errors"][0]["code"], 1)
            self.assertEqual(
                service_manifest["methods"][0]["response"]["fields"][2]["name"],
                "sample_count",
            )
            self.assertEqual(service_manifest["methods"][1]["name"], "ResetCalibration")
            self.assertTrue(service_manifest["methods"][1]["fire_and_forget"])
            self.assertEqual(
                service_manifest["methods"][1]["request"]["fields"][0]["name"],
                "calibration_slot",
            )
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
            self.assertTrue(
                any(record["element"] == "method:UltrasonicDistanceService.GetDistanceStatistics"
                    for record in traceability["records"])
            )
            self.assertTrue(
                any(record["element"] ==
                    "error:UltrasonicDistanceService.GetDistanceStatistics.WindowUnavailable"
                    for record in traceability["records"])
            )
            self.assertTrue(
                any(record["element"] == "service-field:UltrasonicDistanceService.CalibrationMode"
                    for record in traceability["records"])
            )
            self.assertTrue(
                any(record["element"] == "trigger:UltrasonicDistanceService.ObstacleCleared"
                    for record in traceability["records"])
            )
            self.assertTrue(
                any(record["element"].endswith(".GetDistanceStatistics.sample_count")
                    for record in traceability["records"])
            )
            self.assertTrue(
                any(record["element"] == "method:UltrasonicDistanceService.ResetCalibration"
                    for record in traceability["records"])
            )
            self.assertTrue(
                any(record["element"].endswith(".ResetCalibration.calibration_slot")
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

    def test_fire_and_forget_method_rejects_response_fields(self) -> None:
        invalid_model = {
            "service": {
                "name": "InvalidMethodService",
                "instance": "/OpenAUTOSAR/Vehicle/Ultrasonic",
                "methods": [
                    {
                        "name": "ResetCalibration",
                        "fire_and_forget": True,
                        "response": [{"name": "accepted", "type": "bool"}],
                    }
                ],
            }
        }

        with tempfile.TemporaryDirectory() as temp:
            model_dir = Path(temp) / "model"
            model_dir.mkdir()
            path = model_dir / "invalid_method.json"
            path.write_text(json.dumps(invalid_model, indent=2), encoding="utf-8")

            with self.assertRaises(ModelError) as context:
                load_model(model_dir)
            self.assertIn("invalid_method.json", str(context.exception))
            self.assertIn(
                "fire-and-forget method cannot define response fields",
                str(context.exception),
            )

    def test_fire_and_forget_method_rejects_errors(self) -> None:
        invalid_model = {
            "service": {
                "name": "InvalidMethodErrorService",
                "instance": "/OpenAUTOSAR/Vehicle/Ultrasonic",
                "methods": [
                    {
                        "name": "ResetCalibration",
                        "fire_and_forget": True,
                        "errors": [{"name": "Rejected", "code": 1}],
                    }
                ],
            }
        }

        with tempfile.TemporaryDirectory() as temp:
            model_dir = Path(temp) / "model"
            model_dir.mkdir()
            path = model_dir / "invalid_method_error.json"
            path.write_text(json.dumps(invalid_model, indent=2), encoding="utf-8")

            with self.assertRaises(ModelError) as context:
                load_model(model_dir)
            self.assertIn("invalid_method_error.json", str(context.exception))
            self.assertIn(
                "fire-and-forget method cannot define errors",
                str(context.exception),
            )

    def test_method_errors_reject_duplicate_codes(self) -> None:
        invalid_model = {
            "service": {
                "name": "DuplicateMethodErrorService",
                "instance": "/OpenAUTOSAR/Vehicle/Ultrasonic",
                "methods": [
                    {
                        "name": "GetDistanceStatistics",
                        "errors": [
                            {"name": "WindowUnavailable", "code": 1},
                            {"name": "SensorDegraded", "code": 1},
                        ],
                    }
                ],
            }
        }

        with tempfile.TemporaryDirectory() as temp:
            model_dir = Path(temp) / "model"
            model_dir.mkdir()
            path = model_dir / "duplicate_method_error.json"
            path.write_text(json.dumps(invalid_model, indent=2), encoding="utf-8")

            with self.assertRaises(ModelError) as context:
                load_model(model_dir)
            self.assertIn("duplicate_method_error.json", str(context.exception))
            self.assertIn("duplicate method error code", str(context.exception))

    def test_service_field_rejects_disabled_accessors(self) -> None:
        invalid_model = {
            "service": {
                "name": "InvalidFieldService",
                "instance": "/OpenAUTOSAR/Vehicle/Ultrasonic",
                "fields": [
                    {
                        "name": "CalibrationMode",
                        "type": "uint8",
                        "getter": False,
                        "setter": False,
                        "notifier": False,
                    }
                ],
            }
        }

        with tempfile.TemporaryDirectory() as temp:
            model_dir = Path(temp) / "model"
            model_dir.mkdir()
            path = model_dir / "invalid_field.json"
            path.write_text(json.dumps(invalid_model, indent=2), encoding="utf-8")

            with self.assertRaises(ModelError) as context:
                load_model(model_dir)
            self.assertIn("invalid_field.json", str(context.exception))
            self.assertIn(
                "service field must enable getter, setter, or notifier",
                str(context.exception),
            )

    def test_service_trigger_rejects_duplicate_names(self) -> None:
        invalid_model = {
            "service": {
                "name": "InvalidTriggerService",
                "instance": "/OpenAUTOSAR/Vehicle/Ultrasonic",
                "triggers": [
                    {"name": "ObstacleCleared"},
                    {"name": "ObstacleCleared"},
                ],
            }
        }

        with tempfile.TemporaryDirectory() as temp:
            model_dir = Path(temp) / "model"
            model_dir.mkdir()
            path = model_dir / "invalid_trigger.json"
            path.write_text(json.dumps(invalid_model, indent=2), encoding="utf-8")

            with self.assertRaises(ModelError) as context:
                load_model(model_dir)
            self.assertIn("invalid_trigger.json", str(context.exception))
            self.assertIn("duplicate service trigger name", str(context.exception))


if __name__ == "__main__":
    unittest.main()
