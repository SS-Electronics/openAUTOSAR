# SPDX-License-Identifier: MIT

from __future__ import annotations

import json
import re
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path
from typing import Any


SENSITIVE_KEY_PATTERN = re.compile(
    r"(^|[_-])(password|secret|credential|token|key|material|certificate)($|[_-])",
    re.IGNORECASE,
)


@dataclass(frozen=True, slots=True)
class InspectionContext:
    repo_root: Path
    generated: Path
    evidence_dir: Path
    test_results_dir: Path
    package_root: Path


def make_context(
    *,
    repo_root: Path,
    generated: Path | None = None,
    evidence_dir: Path | None = None,
    test_results_dir: Path | None = None,
    package_root: Path | None = None,
) -> InspectionContext:
    root = repo_root.resolve()
    return InspectionContext(
        repo_root=root,
        generated=(generated or root / "out/generated").resolve(),
        evidence_dir=(evidence_dir or root / "out/evidence").resolve(),
        test_results_dir=(test_results_dir or root / "out/test-results").resolve(),
        package_root=(package_root or root / "out/package/generic-oem").resolve(),
    )


def platform_status(context: InspectionContext) -> dict[str, Any]:
    machine = machine_show(context)
    services = service_list(context)
    health = health_show(context)
    package = package_list(context)

    missing = []
    if not machine["present"]:
        missing.append("machine-deployment")
    if services["service_count"] == 0:
        missing.append("generated-services")
    if package["artifact_count"] == 0:
        missing.append("package-artifacts")
    if health["status"] != "valid":
        missing.append("health-evidence")

    return {
        "schema": "openautosar.inspection.status.v1",
        "status": "valid" if not missing else "degraded",
        "missing": missing,
        "machine": machine["machine"],
        "target": machine["target"],
        "service_count": services["service_count"],
        "package_artifact_count": package["artifact_count"],
        "health_status": health["status"],
    }


def machine_show(context: InspectionContext) -> dict[str, Any]:
    path = context.repo_root / "deployment/machine/qemux86-64-agl-unagi.yaml"
    lines = _yaml_lines(path)
    return {
        "schema": "openautosar.inspection.machine.v1",
        "present": path.is_file(),
        "path": _display(path, context.repo_root),
        "target": _scalar(lines, "target"),
        "status": _scalar(lines, "status"),
        "machine": _scalar(lines, "machine"),
        "hardware_scope": _scalar(lines, "hardware_scope"),
        "purpose": _list_after(lines, "purpose"),
        "agl": {
            "release_name": _scalar(lines, "release_name"),
            "release_version": _scalar(lines, "release_version"),
            "branch": _scalar(lines, "branch"),
            "manifest": _scalar(lines, "manifest"),
        },
        "yocto": {"release": _scalar(lines, "release")},
        "systemd_units": _list_after(lines, "starts"),
        "network": {
            "tap": _scalar(lines, "tap"),
            "can": _scalar(lines, "can"),
            "firewall_output": _scalar(lines, "firewall_output"),
        },
        "resource_policy": {
            "user": _scalar(lines, "user"),
            "memory_max": _scalar(lines, "memory_max"),
            "tasks_max": _scalar(lines, "tasks_max"),
            "no_new_privileges": _scalar(lines, "no_new_privileges"),
        },
    }


def process_list(context: InspectionContext) -> dict[str, Any]:
    processes: dict[str, dict[str, Any]] = {}
    unit_path = context.repo_root / (
        "meta-openautosar/recipes-platform/openautosar-runtime/files/"
        "openautosar-bootstrap.service"
    )
    exec_start = _systemd_value(unit_path, "ExecStart")
    if exec_start:
        name = Path(exec_start).name
        processes[name] = {
            "name": name,
            "state": "configured",
            "unit": unit_path.name,
            "source": _display(unit_path, context.repo_root),
        }

    bin_dir = context.package_root / "bin"
    if bin_dir.is_dir():
        for path in sorted(bin_dir.iterdir()):
            if not path.is_file():
                continue
            record = processes.setdefault(
                path.name,
                {
                    "name": path.name,
                    "state": "packaged",
                    "unit": "",
                    "source": _display(path, context.repo_root),
                },
            )
            record["packaged"] = True
            record["binary"] = _display(path, context.repo_root)

    return {
        "schema": "openautosar.inspection.process-list.v1",
        "process_count": len(processes),
        "processes": sorted(processes.values(), key=lambda item: item["name"]),
    }


def process_inspect(context: InspectionContext, name: str) -> dict[str, Any]:
    for process in process_list(context)["processes"]:
        if process["name"] == name:
            return {
                "schema": "openautosar.inspection.process.v1",
                "found": True,
                "process": process,
            }

    return {
        "schema": "openautosar.inspection.process.v1",
        "found": False,
        "process": {"name": name},
    }


def function_group_list(context: InspectionContext) -> dict[str, Any]:
    groups = _function_groups(context)
    return {
        "schema": "openautosar.inspection.function-groups.v1",
        "function_group_count": len(groups),
        "function_groups": groups,
    }


def state_show(context: InspectionContext) -> dict[str, Any]:
    groups = _function_groups(context)
    states = [
        {
            "name": group["name"],
            "current_state": group["initial_state"],
            "source": "policy-initial-state",
        }
        for group in groups
    ]
    return {
        "schema": "openautosar.inspection.state.v1",
        "mode": "offline-policy-snapshot",
        "function_groups": states,
    }


def service_list(context: InspectionContext) -> dict[str, Any]:
    summary = _read_json(context.generated / "manifest-summary.json")
    services = summary.get("services", []) if isinstance(summary, dict) else []
    normalized = []
    for service in services if isinstance(services, list) else []:
        if not isinstance(service, dict):
            continue
        normalized.append(
            {
                "name": service.get("name", ""),
                "instance": service.get("instance", ""),
                "source": service.get("source", ""),
                "event_count": service.get("event_count", 0),
                "e2e_event_count": service.get("e2e_event_count", 0),
                "events": service.get("events", []),
            }
        )

    return {
        "schema": "openautosar.inspection.service-list.v1",
        "generated": _display(context.generated, context.repo_root),
        "service_count": len(normalized),
        "services": normalized,
    }


def service_watch(context: InspectionContext) -> dict[str, Any]:
    services = service_list(context)
    return {
        "schema": "openautosar.inspection.service-watch.v1",
        "mode": "snapshot",
        "service_count": services["service_count"],
        "services": services["services"],
    }


def health_show(context: InspectionContext) -> dict[str, Any]:
    checks = [
        _json_check(
            "yocto-layer",
            context.evidence_dir / "yocto-layer/validation.json",
            expected_status="valid",
        ),
        _json_check("qemu-dry-run", context.evidence_dir / "qemu/qemu-command.json"),
        _json_check("network-lab", context.evidence_dir / "network-lab/last-run/run.json"),
        _json_check(
            "classic-integration",
            context.evidence_dir / "classic-integration/validation.json",
            expected_status="valid",
        ),
    ]
    checks.extend(_ctest_checks(context))
    failed = [item for item in checks if item["status"] not in {"valid", "present", "passed"}]
    return {
        "schema": "openautosar.inspection.health.v1",
        "status": "valid" if not failed else "degraded",
        "check_count": len(checks),
        "failed_checks": [item["name"] for item in failed],
        "checks": checks,
    }


def package_list(context: InspectionContext) -> dict[str, Any]:
    artifacts = []
    if context.package_root.is_dir():
        patterns = [
            ("binary", "bin/*"),
            ("library", "lib/libopenautosar*.a"),
            ("sdk-header", "include/**/*.h"),
            ("policy", "share/openautosar/**/*.yaml"),
            ("generated", "share/openautosar/generated/**/*"),
            ("architecture-decision", "share/openautosar/work-products/architecture-decisions/*"),
            ("program-governance", "share/openautosar/work-products/governance/*"),
            ("platform-service-doc", "share/openautosar/work-products/platform-services/*"),
            ("implementation-plan", "share/openautosar/work-products/implementation/*"),
            ("backlog", "share/openautosar/work-products/backlog/*"),
            ("implementation-workstream", "share/openautosar/work-products/planning/*"),
            ("source-baseline", "share/openautosar/work-products/requirements/autosar/*"),
            ("classic-integration", "share/openautosar/classic-integration/**/*"),
            ("supportability", "share/openautosar/debug-bundle/*.json"),
        ]
        for category, pattern in patterns:
            for path in sorted(context.package_root.glob(pattern)):
                if path.is_file():
                    artifacts.append(
                        {
                            "category": category,
                            "path": _display(path, context.repo_root),
                            "name": path.name,
                        }
                    )

    return {
        "schema": "openautosar.inspection.package-list.v1",
        "package_root": _display(context.package_root, context.repo_root),
        "artifact_count": len(artifacts),
        "artifacts": artifacts,
    }


def update_status(context: InspectionContext) -> dict[str, Any]:
    policy = context.repo_root / (
        "meta-openautosar/recipes-platform/openautosar-vehicle-ucm/files/"
        "openautosar-vehicle-ucm-policy.yaml"
    )
    lines = _yaml_lines(policy)
    provenance = context.package_root / "provenance.json"
    sbom = context.package_root / "share/openautosar/oem-export/sbom.spdx.json"
    return {
        "schema": "openautosar.inspection.update.v1",
        "mode": _scalar(lines, "mode"),
        "target_scope": _scalar(lines, "target_scope"),
        "activation_state": _scalar(lines, "activation_state"),
        "trusted_signers": _list_after(lines, "trusted_signers"),
        "rollback": {
            "coordinated": _scalar(lines, "coordinated_rollback_on_health_failure"),
            "recovery_required": _scalar(lines, "mark_failed_ecu_recovery_required"),
        },
        "package_provenance": provenance.is_file(),
        "sbom": sbom.is_file(),
    }


def diagnostics_status(context: InspectionContext) -> dict[str, Any]:
    log = context.evidence_dir / "network-lab/last-run/gateway-peer.log"
    text = log.read_text(encoding="utf-8") if log.is_file() else ""
    dtc_match = re.search(r"diagnostic_reported_dtcs=(\d+) event_memory=(\d+)", text)
    uds_match = re.search(
        r"uds_response_can_id=(0x[0-9a-fA-F]+) uds_sid=(0x[0-9a-fA-F]+) "
        r"uds_dtc_records=(\d+) diagnostic_event_memory=(\d+)",
        text,
    )
    return {
        "schema": "openautosar.inspection.diagnostics.v1",
        "source": _display(log, context.repo_root),
        "dtc_records_reported": int(dtc_match.group(1)) if dtc_match else 0,
        "event_memory_records": int(dtc_match.group(2)) if dtc_match else 0,
        "uds": {
            "present": uds_match is not None,
            "response_can_id": uds_match.group(1) if uds_match else "",
            "response_sid": uds_match.group(2) if uds_match else "",
            "dtc_records": int(uds_match.group(3)) if uds_match else 0,
            "event_memory": int(uds_match.group(4)) if uds_match else 0,
        },
    }


def trace_export(context: InspectionContext) -> dict[str, Any]:
    traceability = _read_json(context.generated / "traceability/traceability.json")
    generated = _read_json(context.generated / "manifest-summary.json")
    return {
        "schema": "openautosar.inspection.trace-export.v1",
        "traceability": traceability,
        "generated_outputs": generated.get("outputs", []) if isinstance(generated, dict) else [],
        "evidence": {
            "yocto_layer": _display(
                context.evidence_dir / "yocto-layer/validation.json",
                context.repo_root,
            ),
            "network_lab": _display(
                context.evidence_dir / "network-lab/last-run/run.json",
                context.repo_root,
            ),
            "qemu": _display(context.evidence_dir / "qemu/qemu-command.json", context.repo_root),
        },
        "status": "generated" if traceability else "missing-traceability",
    }


def debug_bundle(context: InspectionContext) -> dict[str, Any]:
    machine = machine_show(context)
    processes = process_list(context)
    groups = function_group_list(context)
    state = state_show(context)
    services = service_list(context)
    health = health_show(context)
    package = package_list(context)
    update = update_status(context)
    diagnostics = diagnostics_status(context)
    trace = trace_export(context)

    status = "valid"
    if not machine["present"] or health["status"] != "valid":
        status = "degraded"

    return {
        "schema": "openautosar.debug.bundle.v1",
        "collection_mode": "offline-support-snapshot",
        "status": status,
        "machine_software_inventory": {
            "machine": machine,
            "package": package,
            "provenance": _read_json(context.package_root / "provenance.json"),
        },
        "process_state": processes,
        "service_registry_snapshot": services,
        "health_status": health,
        "resource_usage": _resource_usage(context, machine, processes, package),
        "recent_state_transitions": _state_transition_snapshot(context, groups, state),
        "relevant_logs": _relevant_logs(context),
        "update_history": update,
        "clock_synchronization_status": _clock_synchronization_status(context),
        "diagnostics": diagnostics,
        "traceability": trace,
        "redacted_configuration": _redacted_configuration(context, machine, package),
    }


def _function_groups(context: InspectionContext) -> list[dict[str, Any]]:
    path = context.repo_root / (
        "meta-openautosar/recipes-platform/openautosar-state-management/files/"
        "openautosar-state-management-policy.yaml"
    )
    groups: list[dict[str, Any]] = []
    current: dict[str, Any] | None = None
    in_states = False
    for line in _yaml_lines(path):
        stripped = line.strip()
        if stripped.startswith("- name:"):
            if current is not None:
                groups.append(current)
            current = {
                "name": stripped.split(":", 1)[1].strip(),
                "initial_state": "",
                "states": [],
                "transition_count": 0,
            }
            in_states = False
            continue
        if current is None:
            continue
        if stripped.startswith("initial_state:"):
            current["initial_state"] = stripped.split(":", 1)[1].strip()
        elif stripped == "states:":
            in_states = True
        elif stripped == "transitions:":
            in_states = False
        elif in_states and stripped.startswith("- "):
            current["states"].append(stripped[2:].strip())
        elif stripped.startswith("- from:"):
            current["transition_count"] += 1
    if current is not None:
        groups.append(current)
    return groups


def _resource_usage(
    context: InspectionContext,
    machine: dict[str, Any],
    processes: dict[str, Any],
    package: dict[str, Any],
) -> dict[str, Any]:
    process_policy = _policy_path(
        context,
        "meta-openautosar/recipes-platform/openautosar-process-launcher/files/"
        "openautosar-process-launcher-policy.yaml",
        "share/openautosar/execution/openautosar-process-launcher-policy.yaml",
    )
    lines = _yaml_lines(process_policy)
    return {
        "schema": "openautosar.debug.resource-usage.v1",
        "mode": "configured-limits",
        "process_count": processes["process_count"],
        "package_artifact_count": package["artifact_count"],
        "machine_limits": machine["resource_policy"],
        "process_launcher": {
            "source": _display(process_policy, context.repo_root),
            "present": process_policy.is_file(),
            "cpu_weight": _scalar(lines, "cpu_weight"),
            "file_descriptor_limit": _scalar(lines, "file_descriptor_limit"),
            "memory_max_bytes": _scalar(lines, "memory_max_bytes"),
            "no_new_privileges": _scalar(lines, "no_new_privileges"),
        },
    }


def _state_transition_snapshot(
    context: InspectionContext,
    groups: dict[str, Any],
    state: dict[str, Any],
) -> dict[str, Any]:
    path = _policy_path(
        context,
        "meta-openautosar/recipes-platform/openautosar-state-management/files/"
        "openautosar-state-management-policy.yaml",
        "share/openautosar/state/openautosar-state-management-policy.yaml",
    )
    transitions = _state_transitions(path)
    return {
        "schema": "openautosar.debug.state-transitions.v1",
        "mode": "offline-policy-snapshot",
        "source": _display(path, context.repo_root),
        "present": path.is_file(),
        "function_group_count": groups["function_group_count"],
        "current_state": state["function_groups"],
        "transition_count": len(transitions),
        "transitions": transitions,
    }


def _state_transitions(path: Path) -> list[dict[str, str]]:
    transitions: list[dict[str, str]] = []
    current_group = ""
    current: dict[str, str] | None = None
    in_transitions = False

    for line in _yaml_lines(path):
        stripped = line.strip()
        indent = len(line) - len(line.lstrip())
        if stripped.startswith("- name:"):
            if current is not None:
                transitions.append(current)
                current = None
            current_group = stripped.split(":", 1)[1].strip()
            in_transitions = False
            continue
        if stripped == "transitions:":
            in_transitions = True
            continue
        if in_transitions and indent <= 2 and not stripped.startswith("- from:"):
            if current is not None:
                transitions.append(current)
                current = None
            in_transitions = False
        if not in_transitions:
            continue
        if stripped.startswith("- from:"):
            if current is not None:
                transitions.append(current)
            current = {
                "function_group": current_group,
                "from": stripped.split(":", 1)[1].strip(),
                "to": "",
                "required_priority": "",
                "failure_state": "",
            }
        elif current is not None and stripped.startswith("to:"):
            current["to"] = stripped.split(":", 1)[1].strip()
        elif current is not None and stripped.startswith("required_priority:"):
            current["required_priority"] = stripped.split(":", 1)[1].strip()
        elif current is not None and stripped.startswith("failure_state:"):
            current["failure_state"] = stripped.split(":", 1)[1].strip()

    if current is not None:
        transitions.append(current)
    return transitions


def _relevant_logs(context: InspectionContext) -> dict[str, Any]:
    log_dir = context.evidence_dir / "network-lab/last-run"
    logs = []
    for path in sorted(log_dir.glob("*.log")) if log_dir.is_dir() else []:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        logs.append(
            {
                "path": _display(path, context.repo_root),
                "present": True,
                "line_count": len(lines),
                "excerpt": [_redact_text(line)[:200] for line in lines[-20:]],
            }
        )
    return {
        "schema": "openautosar.debug.logs.v1",
        "mode": "bounded-redacted-excerpt",
        "log_count": len(logs),
        "logs": logs,
    }


def _clock_synchronization_status(context: InspectionContext) -> dict[str, Any]:
    path = _policy_path(
        context,
        "meta-openautosar/recipes-platform/openautosar-time-network/files/"
        "openautosar-time-network-policy.yaml",
        "share/openautosar/network/openautosar-time-network-policy.yaml",
    )
    lines = _yaml_lines(path)
    domains = [
        line.strip().split(":", 1)[1].strip()
        for line in lines
        if line.strip().startswith("- id:")
    ]
    return {
        "schema": "openautosar.debug.clock-sync.v1",
        "source": _display(path, context.repo_root),
        "present": path.is_file(),
        "virtual_clock_enabled": _scalar(lines, "enabled"),
        "clock_source": _scalar(lines, "source"),
        "domain_count": len(domains),
        "domains": domains,
        "max_uncertainty_ns": _scalar(lines, "max_uncertainty_ns"),
        "loss_of_sync_timeout_ns": _scalar(lines, "loss_of_sync_timeout_ns"),
        "jump_policy": _scalar(lines, "jump_policy"),
    }


def _redacted_configuration(
    context: InspectionContext,
    machine: dict[str, Any],
    package: dict[str, Any],
) -> dict[str, Any]:
    policy_paths = [
        _policy_path(
            context,
            "deployment/machine/qemux86-64-agl-unagi.yaml",
            "share/openautosar/deployment/machine/qemux86-64-agl-unagi.yaml",
        ),
        _policy_path(
            context,
            "meta-openautosar/recipes-security/openautosar-security/files/"
            "openautosar-security-policy.yaml",
            "share/openautosar/security/openautosar-security-policy.yaml",
        ),
        _policy_path(
            context,
            "meta-openautosar/recipes-security/openautosar-crypto/files/"
            "openautosar-crypto-policy.yaml",
            "share/openautosar/security/openautosar-crypto-policy.yaml",
        ),
        _policy_path(
            context,
            "meta-openautosar/recipes-platform/openautosar-time-network/files/"
            "openautosar-time-network-policy.yaml",
            "share/openautosar/network/openautosar-time-network-policy.yaml",
        ),
    ]
    return {
        "schema": "openautosar.debug.redacted-configuration.v1",
        "redaction": {
            "status": "applied",
            "replacement": "<redacted>",
        },
        "machine": _redact_value(machine),
        "package_root": package["package_root"],
        "policy_files": [_policy_snapshot(context, path) for path in policy_paths],
    }


def _policy_snapshot(context: InspectionContext, path: Path) -> dict[str, Any]:
    entries = []
    for line in _yaml_lines(path)[:120]:
        stripped = line.strip()
        match = re.match(r"^-?\s*([A-Za-z0-9_-]+):\s*(.+)$", stripped)
        if not match:
            continue
        key = match.group(1)
        entries.append({"key": key, "value": _redact_scalar(key, match.group(2).strip())})
    return {
        "path": _display(path, context.repo_root),
        "present": path.is_file(),
        "entries": entries,
    }


def _policy_path(context: InspectionContext, repo_relative: str, package_relative: str) -> Path:
    repo_path = context.repo_root / repo_relative
    if repo_path.is_file():
        return repo_path
    return context.package_root / package_relative


def _redact_value(value: Any) -> Any:
    if isinstance(value, dict):
        redacted = {}
        for key, item in value.items():
            redacted[key] = _redact_scalar(key, item)
        return redacted
    if isinstance(value, list):
        return [_redact_value(item) for item in value]
    return value


def _redact_scalar(key: str, value: Any) -> Any:
    if SENSITIVE_KEY_PATTERN.search(key):
        return "<redacted>"
    if isinstance(value, dict) or isinstance(value, list):
        return _redact_value(value)
    if isinstance(value, str):
        return _redact_text(value)
    return value


def _redact_text(value: str) -> str:
    return re.sub(
        r"(?i)(password|secret|credential|token|key|material|certificate)=\S+",
        r"\1=<redacted>",
        value,
    )


def _json_check(name: str, path: Path, *, expected_status: str | None = None) -> dict[str, Any]:
    data = _read_json(path)
    if not data:
        status = "missing"
    elif expected_status is not None:
        status = "valid" if data.get("status") == expected_status else "degraded"
    else:
        status = "present"
    return {"name": name, "path": str(path), "status": status}


def _ctest_checks(context: InspectionContext) -> list[dict[str, Any]]:
    checks = []
    for path in sorted(context.test_results_dir.rglob("ctest.xml")):
        try:
            root = ET.parse(path).getroot()
        except ET.ParseError:
            checks.append({"name": path.parent.name, "path": str(path), "status": "invalid"})
            continue
        failures = int(root.attrib.get("failures", "0"))
        errors = int(root.attrib.get("errors", "0"))
        status = "passed" if failures == 0 and errors == 0 else "failed"
        checks.append(
            {
                "name": f"ctest-{path.parent.name}",
                "path": _display(path, context.repo_root),
                "status": status,
                "failures": failures,
                "errors": errors,
            }
        )
    return checks


def _read_json(path: Path) -> dict[str, Any]:
    if not path.is_file():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return {}
    return data if isinstance(data, dict) else {}


def _yaml_lines(path: Path) -> list[str]:
    if not path.is_file():
        return []
    lines = []
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        lines.append(line.rstrip())
    return lines


def _scalar(lines: list[str], key: str) -> str:
    pattern = re.compile(rf"^\s*{re.escape(key)}:\s*(.+?)\s*$")
    for line in lines:
        match = pattern.match(line)
        if match:
            return match.group(1).strip('"')
    return ""


def _list_after(lines: list[str], key: str) -> list[str]:
    values = []
    in_list = False
    base_indent = 0
    for line in lines:
        stripped = line.strip()
        if not in_list:
            if stripped == f"{key}:":
                in_list = True
                base_indent = len(line) - len(line.lstrip())
            continue
        indent = len(line) - len(line.lstrip())
        if indent <= base_indent and not stripped.startswith("- "):
            break
        if stripped.startswith("- "):
            values.append(stripped[2:].strip())
    return values


def _systemd_value(path: Path, key: str) -> str:
    if not path.is_file():
        return ""
    prefix = f"{key}="
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith(prefix):
            return line[len(prefix):].strip()
    return ""


def _display(path: Path, repo_root: Path) -> str:
    try:
        return path.resolve().relative_to(repo_root.resolve()).as_posix()
    except ValueError:
        return str(path)
