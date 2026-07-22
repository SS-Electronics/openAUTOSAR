# SPDX-License-Identifier: MIT

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


REQUIRED_EXTERNAL_SYSTEMS = {
    "AUTOSAR Classic ECU",
    "diagnostic tester",
    "backend/cloud system",
    "other Adaptive machine",
    "non-AUTOSAR application",
    "HMI system",
}
REQUIRED_ROUTE_TYPES = {
    "ultrasonic signal-to-service mapping",
    "service-to-signal mapping for control/status tests",
    "SOME/IP-to-CAN/CAN-FD-style mapping",
    "diagnostics routing",
    "time and alive-counter validation",
    "update status",
    "vehicle state",
    "Function Group state",
}
REQUIRED_SIGNAL_NAMES = {
    "sensor_id",
    "distance_mm",
    "quality",
    "timestamp_ns",
    "alive_counter",
    "diagnostic_status",
    "crc16_ccitt",
}


class ClassicIntegrationError(ValueError):
    """Raised when Classic/external-system integration metadata is invalid."""


def validate_classic_integration(
    *,
    repo_root: Path,
    config: Path,
    generated: Path,
    evidence_dir: Path,
    tool_version: str,
) -> dict[str, Any]:
    repo_root = repo_root.resolve()
    config = config.resolve()
    generated = generated.resolve()
    evidence_dir = evidence_dir.resolve()

    missing: list[str] = []
    data = _read_json(config, missing, "classic-gateway-config")
    signal_db_path = _signal_database_path(repo_root, data, missing)
    signal_db = _read_json(signal_db_path, missing, "classic-signal-database")

    _validate_config(data, signal_db, missing)
    route_plan = _route_plan(data, signal_db, repo_root, config, signal_db_path, tool_version)
    runtime_config = _runtime_config(data, signal_db, tool_version)
    signal_report = _signal_report(data, signal_db, repo_root, signal_db_path)

    generated.mkdir(parents=True, exist_ok=True)
    _write_json(route_plan, generated / "gateway-route-plan.json")
    _write_json(signal_report, generated / "signal-database-validation.json")
    (generated / "gateway-runtime.conf").write_text(runtime_config, encoding="utf-8")

    evidence = {
        "schema": "openautosar.classic-integration.validation.v1",
        "status": "valid" if not missing else "invalid",
        "config": _display(config, repo_root),
        "generated": _display(generated, repo_root),
        "signal_database": _display(signal_db_path, repo_root),
        "external_system_count": len(_list(data.get("external_systems"))),
        "route_count": len(_list(data.get("routes"))),
        "required_external_system_count": len(REQUIRED_EXTERNAL_SYSTEMS),
        "required_route_type_count": len(REQUIRED_ROUTE_TYPES),
        "signal_count": len(_list(signal_db.get("signals"))),
        "software_in_loop": _bool_path(data, ["simulation", "mode"]) == "software-in-the-loop",
        "socketcan_vcan": _bool_path(data, ["transport", "socketcan_interface"]) == "vcan0",
        "missing": sorted(missing),
    }
    _write_json(evidence, evidence_dir / "validation.json")

    if missing:
        raise ClassicIntegrationError(
            "Classic integration metadata invalid: " + ", ".join(sorted(missing))
        )
    return evidence


def _validate_config(data: dict[str, Any], signal_db: dict[str, Any], missing: list[str]) -> None:
    _require(data.get("schema") == "openautosar.classic-gateway.config.v1", missing, "schema")
    _require(data.get("license") == "SPDX-License-Identifier: MIT", missing, "license")
    _require(_bool_path(data, ["simulation", "mode"]) == "software-in-the-loop", missing, "sil")
    _require(_bool_path(data, ["simulation", "requires_mcu"]) is False, missing, "no-mcu")
    _require(
        _bool_path(data, ["simulation", "requires_commercial_classic_stack"]) is False,
        missing,
        "no-commercial-classic-stack",
    )
    _require(_bool_path(data, ["transport", "socketcan_interface"]) == "vcan0", missing, "vcan")
    _require(
        _bool_path(data, ["transport", "type"]) == signal_db.get("frame", {}).get("transport"),
        missing,
        "transport-match",
    )
    _require(_int_path(data, ["transport", "pdu_size_bytes"]) == 23, missing, "pdu-size")
    _require(signal_db.get("name") == "openautosar.ultrasonic.classic.v1", missing, "signal-db")
    _require(signal_db.get("frame", {}).get("schema_version") == 1, missing, "signal-db-version")
    _require(signal_db.get("frame", {}).get("size_bytes") == 23, missing, "signal-db-size")

    external_systems = {
        _string(item.get("name"))
        for item in _list(data.get("external_systems"))
        if isinstance(item, dict)
    }
    for item in REQUIRED_EXTERNAL_SYSTEMS:
        _require(item in external_systems, missing, f"external-system:{item}")

    route_types = {
        _string(item.get("type")) for item in _list(data.get("routes")) if isinstance(item, dict)
    }
    for item in REQUIRED_ROUTE_TYPES:
        _require(item in route_types, missing, f"route:{item}")

    signal_names = {
        _string(item.get("name"))
        for item in _list(signal_db.get("signals"))
        if isinstance(item, dict)
    }
    for item in REQUIRED_SIGNAL_NAMES:
        _require(item in signal_names, missing, f"signal:{item}")


def _signal_database_path(
    repo_root: Path,
    data: dict[str, Any],
    missing: list[str],
) -> Path:
    signal_db = data.get("signal_database")
    if not isinstance(signal_db, dict):
        missing.append("signal-database-reference")
        return repo_root / "missing"
    path = signal_db.get("path")
    if not isinstance(path, str) or not path:
        missing.append("signal-database-path")
        return repo_root / "missing"
    return repo_root / path


def _route_plan(
    data: dict[str, Any],
    signal_db: dict[str, Any],
    repo_root: Path,
    config: Path,
    signal_db_path: Path,
    tool_version: str,
) -> dict[str, Any]:
    routes = []
    for route in _list(data.get("routes")):
        if not isinstance(route, dict):
            continue
        routes.append(
            {
                "id": _string(route.get("id")),
                "source": _string(route.get("source")),
                "status": _string(route.get("status")),
                "target": _string(route.get("target")),
                "type": _string(route.get("type")),
            }
        )

    return {
        "schema": "openautosar.classic-gateway.route-plan.v1",
        "status": "generated",
        "tool_version": tool_version,
        "config": _display(config, repo_root),
        "signal_database": _display(signal_db_path, repo_root),
        "transport": data.get("transport", {}),
        "service": data.get("service", {}),
        "validation": data.get("validation", {}),
        "signal_count": len(_list(signal_db.get("signals"))),
        "route_count": len(routes),
        "routes": sorted(routes, key=lambda item: item["id"]),
    }


def _runtime_config(data: dict[str, Any], signal_db: dict[str, Any], tool_version: str) -> str:
    service = data.get("service", {}) if isinstance(data.get("service"), dict) else {}
    runtime = data.get("runtime", {}) if isinstance(data.get("runtime"), dict) else {}
    transport = data.get("transport", {}) if isinstance(data.get("transport"), dict) else {}
    validation = data.get("validation", {}) if isinstance(data.get("validation"), dict) else {}
    frame = signal_db.get("frame", {}) if isinstance(signal_db.get("frame"), dict) else {}
    lines = [
        "schema=openautosar.classic-gateway.runtime.v1",
        f"tool_version={tool_version}",
        f"can_interface={_string(transport.get('socketcan_interface'))}",
        f"ultrasonic_can_id={_string(transport.get('ultrasonic_can_id'))}",
        f"ultrasonic_pdu_size={_int(frame.get('size_bytes'))}",
        f"service_interface_id={_string(service.get('interface_id'))}",
        f"service_instance_id={_string(service.get('instance_id'))}",
        f"service_major_version={_int(service.get('major_version'))}",
        f"service_minor_version={_int(service.get('minor_version'))}",
        f"event_name={_string(service.get('event_name'))}",
        f"process_identity={_string(runtime.get('process_identity'))}",
        f"machine_identity={_string(runtime.get('machine_identity'))}",
        f"endpoint_address={_string(runtime.get('endpoint'))}",
        f"access_policy={_string(runtime.get('access_policy'))}",
        f"deployment_provenance={_string(runtime.get('deployment_provenance'))}",
        f"max_sample_age_ns={_int(validation.get('max_sample_age_ns'))}",
        "",
    ]
    return "\n".join(lines)


def _signal_report(
    data: dict[str, Any],
    signal_db: dict[str, Any],
    repo_root: Path,
    signal_db_path: Path,
) -> dict[str, Any]:
    signals = []
    for signal in _list(signal_db.get("signals")):
        if not isinstance(signal, dict):
            continue
        signals.append(
            {
                "name": _string(signal.get("name")),
                "offset": _int(signal.get("offset")),
                "type": _string(signal.get("type")),
            }
        )
    return {
        "schema": "openautosar.classic-gateway.signal-database.validation.v1",
        "status": "valid",
        "signal_database": _display(signal_db_path, repo_root),
        "configured_database": data.get("signal_database", {}),
        "frame": signal_db.get("frame", {}),
        "signals": signals,
    }


def _read_json(path: Path, missing: list[str], name: str) -> dict[str, Any]:
    if not path.is_file():
        missing.append(name)
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        missing.append(f"{name}:json")
        return {}
    if not isinstance(data, dict):
        missing.append(f"{name}:object")
        return {}
    return data


def _require(condition: bool, missing: list[str], name: str) -> None:
    if not condition:
        missing.append(name)


def _list(value: Any) -> list[Any]:
    return value if isinstance(value, list) else []


def _string(value: Any) -> str:
    return value if isinstance(value, str) else ""


def _int(value: Any) -> int:
    return value if isinstance(value, int) else 0


def _bool_path(data: dict[str, Any], path: list[str]) -> Any:
    value: Any = data
    for key in path:
        if not isinstance(value, dict):
            return None
        value = value.get(key)
    return value


def _int_path(data: dict[str, Any], path: list[str]) -> int:
    value = _bool_path(data, path)
    return value if isinstance(value, int) else 0


def _write_json(data: dict[str, Any], output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _display(path: Path, repo_root: Path) -> str:
    try:
        return path.resolve().relative_to(repo_root.resolve()).as_posix()
    except ValueError:
        return str(path)
