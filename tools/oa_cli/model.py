# SPDX-License-Identifier: MIT

from __future__ import annotations

import json
import re
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path
from typing import Any


MODEL_SUFFIXES = {".arxml", ".json", ".yaml", ".yml"}
SUPPORTED_FIELD_TYPES = {
    "bool": "bool",
    "float32": "float",
    "float64": "double",
    "uint8": "std::uint8_t",
    "uint16": "std::uint16_t",
    "uint32": "std::uint32_t",
    "uint64": "std::uint64_t",
}
E2E_PROFILES = {"profile01", "profile05"}
E2E_COUNTER_WIDTHS = {4, 8, 16, 32}


class ModelError(ValueError):
    """Validation error raised while loading model inputs."""

    def __init__(self, source: Path, pointer: str, message: str) -> None:
        self.source = source
        self.pointer = pointer
        self.message = message
        super().__init__(f"{source}:{pointer}: {message}")


@dataclass(frozen=True, slots=True)
class SourceRef:
    path: str
    pointer: str


@dataclass(frozen=True, slots=True)
class FieldModel:
    name: str
    type_name: str
    source: SourceRef


@dataclass(frozen=True, slots=True)
class E2EProtectionModel:
    profile: str
    data_id: int
    counter_bits: int
    max_delta_counter: int
    timeout_ms: int
    max_repetitions: int
    source: SourceRef


@dataclass(frozen=True, slots=True)
class EventModel:
    name: str
    fields: tuple[FieldModel, ...]
    source: SourceRef
    e2e: E2EProtectionModel | None = None


@dataclass(frozen=True, slots=True)
class ServiceModel:
    name: str
    instance: str
    events: tuple[EventModel, ...]
    source: SourceRef


@dataclass(frozen=True, slots=True)
class ModelBundle:
    root: Path
    files: tuple[Path, ...]
    services: tuple[ServiceModel, ...]


def collect_model_files(model: Path) -> list[Path]:
    return sorted(
        path
        for path in model.rglob("*")
        if path.is_file() and path.suffix.lower() in MODEL_SUFFIXES
    )


def load_model(model: Path) -> ModelBundle:
    root = model.resolve()
    if not root.exists():
        raise SystemExit(f"model path does not exist: {root}")
    if not root.is_dir():
        raise SystemExit(f"model path is not a directory: {root}")

    files = collect_model_files(root)
    services: list[ServiceModel] = []
    for path in files:
        if path.suffix == ".json":
            services.extend(_parse_json_model(root, path))
        elif path.suffix == ".arxml":
            services.extend(_parse_arxml_model(root, path))
        else:
            _validate_metadata_file(path)

    bundle = ModelBundle(root=root, files=tuple(files), services=tuple(services))
    _validate_bundle(bundle)
    return bundle


def write_generated_model(bundle: ModelBundle, output: Path, tool_version: str) -> dict[str, Any]:
    output.mkdir(parents=True, exist_ok=True)
    generated_outputs: list[dict[str, str]] = []
    traceability: list[dict[str, str]] = []

    for service in sorted(bundle.services, key=lambda item: item.name):
        manifest_path = output / "services" / f"{_snake_case(service.name)}.json"
        header_path = output / "include" / "openautosar" / "generated" / (
            f"{_snake_case(service.name)}.h"
        )
        _write_json(_service_manifest(service), manifest_path)
        header_path.parent.mkdir(parents=True, exist_ok=True)
        header_path.write_text(_service_header(service), encoding="utf-8")

        manifest_relative = manifest_path.relative_to(output).as_posix()
        header_relative = header_path.relative_to(output).as_posix()
        generated_outputs.append(
            {
                "name": f"{service.name}-manifest",
                "path": manifest_relative,
                "status": "generated",
            }
        )
        generated_outputs.append(
            {
                "name": f"{service.name}-service-types",
                "path": header_relative,
                "status": "generated",
            }
        )
        traceability.extend(_traceability_records(service, manifest_relative, header_relative))

    traceability_path = output / "traceability" / "traceability.json"
    _write_json({"records": traceability, "status": "generated"}, traceability_path)
    generated_outputs.append(
        {
            "name": "traceability",
            "path": traceability_path.relative_to(output).as_posix(),
            "status": "generated",
        }
    )

    summary = {
        "generated_by": f"oa-cli {tool_version}",
        "model": str(bundle.root),
        "model_files": [path.relative_to(bundle.root).as_posix() for path in bundle.files],
        "outputs": generated_outputs,
        "service_count": len(bundle.services),
        "services": [_service_summary(service) for service in bundle.services],
    }
    _write_json(summary, output / "manifest-summary.json")
    return summary


def validation_summary(bundle: ModelBundle) -> dict[str, Any]:
    return {
        "model": str(bundle.root),
        "model_files": [path.relative_to(bundle.root).as_posix() for path in bundle.files],
        "service_count": len(bundle.services),
        "services": [_service_summary(service) for service in bundle.services],
        "status": "valid",
    }


def _parse_json_model(root: Path, path: Path) -> list[ServiceModel]:
    try:
        with path.open("r", encoding="utf-8") as handle:
            data = json.load(handle)
    except json.JSONDecodeError as exc:
        raise ModelError(path, f"line {exc.lineno}", exc.msg) from exc

    services = data.get("services")
    if services is None and "service" in data:
        services = [data["service"]]
    if not isinstance(services, list) or not services:
        raise ModelError(path, "/service", "model must define at least one service")

    result: list[ServiceModel] = []
    for index, service_data in enumerate(services):
        result.append(_json_service(root, path, service_data, f"/services/{index}"))
    return result


def _json_service(
    root: Path,
    path: Path,
    data: Any,
    pointer: str,
) -> ServiceModel:
    if not isinstance(data, dict):
        raise ModelError(path, pointer, "service entry must be an object")

    name = _required_string(path, data, "name", f"{pointer}/name")
    instance = _required_string(path, data, "instance", f"{pointer}/instance")
    events_data = data.get("events")
    if not isinstance(events_data, list) or not events_data:
        raise ModelError(path, f"{pointer}/events", "service must define at least one event")

    events: list[EventModel] = []
    for event_index, event_data in enumerate(events_data):
        event_pointer = f"{pointer}/events/{event_index}"
        events.append(_json_event(root, path, event_data, event_pointer))

    return ServiceModel(
        name=name,
        instance=instance,
        events=tuple(events),
        source=_source_ref(root, path, pointer),
    )


def _json_event(root: Path, path: Path, data: Any, pointer: str) -> EventModel:
    if not isinstance(data, dict):
        raise ModelError(path, pointer, "event entry must be an object")

    name = _required_string(path, data, "name", f"{pointer}/name")
    fields_data = data.get("fields")
    if not isinstance(fields_data, list) or not fields_data:
        raise ModelError(path, f"{pointer}/fields", "event must define at least one field")

    fields: list[FieldModel] = []
    for field_index, field_data in enumerate(fields_data):
        field_pointer = f"{pointer}/fields/{field_index}"
        fields.append(_json_field(root, path, field_data, field_pointer))

    e2e = None
    if "e2e" in data:
        e2e = _json_e2e(root, path, data["e2e"], f"{pointer}/e2e")

    return EventModel(
        name=name,
        fields=tuple(fields),
        source=_source_ref(root, path, pointer),
        e2e=e2e,
    )


def _json_field(root: Path, path: Path, data: Any, pointer: str) -> FieldModel:
    if not isinstance(data, dict):
        raise ModelError(path, pointer, "field entry must be an object")

    name = _required_string(path, data, "name", f"{pointer}/name")
    type_name = _required_string(path, data, "type", f"{pointer}/type")
    return FieldModel(name=name, type_name=type_name, source=_source_ref(root, path, pointer))


def _json_e2e(root: Path, path: Path, data: Any, pointer: str) -> E2EProtectionModel:
    if not isinstance(data, dict):
        raise ModelError(path, pointer, "E2E entry must be an object")

    return E2EProtectionModel(
        profile=_required_string(path, data, "profile", f"{pointer}/profile"),
        data_id=_required_int(path, data, "data_id", f"{pointer}/data_id"),
        counter_bits=_optional_int(path, data, "counter_bits", 8, f"{pointer}/counter_bits"),
        max_delta_counter=_optional_int(
            path,
            data,
            "max_delta_counter",
            1,
            f"{pointer}/max_delta_counter",
        ),
        timeout_ms=_optional_int(path, data, "timeout_ms", 100, f"{pointer}/timeout_ms"),
        max_repetitions=_optional_int(
            path,
            data,
            "max_repetitions",
            0,
            f"{pointer}/max_repetitions",
        ),
        source=_source_ref(root, path, pointer),
    )


def _parse_arxml_model(root: Path, path: Path) -> list[ServiceModel]:
    try:
        tree = ET.parse(path)
    except ET.ParseError as exc:
        raise ModelError(path, "xml", str(exc)) from exc

    services: list[ServiceModel] = []
    for service_node in tree.iter():
        if _local_name(service_node.tag) != "SERVICE-INTERFACE":
            continue

        name = _child_text(service_node, "SHORT-NAME")
        if not name:
            raise ModelError(path, _xml_pointer(service_node), "service interface has no name")

        instance = _child_text(service_node, "INSTANCE-SPECIFIER")
        if not instance:
            instance = f"/OpenAUTOSAR/Services/{name}"

        events: list[EventModel] = []
        for event_node in _direct_descendants(service_node, "EVENT"):
            event_name = _child_text(event_node, "SHORT-NAME")
            if not event_name:
                raise ModelError(path, _xml_pointer(event_node), "event has no name")

            fields = tuple(_arxml_fields(root, path, event_node))
            if not fields:
                raise ModelError(path, _xml_pointer(event_node), "event has no fields")
            events.append(
                EventModel(
                    name=event_name,
                    fields=fields,
                    source=_source_ref(root, path, _xml_pointer(event_node)),
                )
            )

        if not events:
            raise ModelError(path, _xml_pointer(service_node), "service interface has no events")
        services.append(
            ServiceModel(
                name=name,
                instance=instance,
                events=tuple(events),
                source=_source_ref(root, path, _xml_pointer(service_node)),
            )
        )

    return services


def _arxml_fields(root: Path, path: Path, event_node: ET.Element) -> list[FieldModel]:
    fields: list[FieldModel] = []
    for field_node in _direct_descendants(event_node, "FIELD"):
        name = _child_text(field_node, "SHORT-NAME")
        type_name = _child_text(field_node, "TYPE")
        if not type_name:
            type_name = _last_ref_segment(_child_text(field_node, "TYPE-TREF"))
        if not name or not type_name:
            raise ModelError(path, _xml_pointer(field_node), "field has no name or type")
        fields.append(
            FieldModel(
                name=name,
                type_name=type_name,
                source=_source_ref(root, path, _xml_pointer(field_node)),
            )
        )
    return fields


def _validate_metadata_file(path: Path) -> None:
    if path.suffix.lower() in {".yaml", ".yml"}:
        return
    raise ModelError(path, "/", "unsupported model file type")


def _validate_bundle(bundle: ModelBundle) -> None:
    service_names: set[str] = set()
    service_instances: set[str] = set()
    for service in bundle.services:
        _validate_identifier(service.source, service.name, "service name")
        _validate_instance(service.source, service.instance)
        if service.name in service_names:
            raise _source_error(service.source, "duplicate service name")
        if service.instance in service_instances:
            raise _source_error(service.source, "duplicate service instance")
        service_names.add(service.name)
        service_instances.add(service.instance)

        event_names: set[str] = set()
        for event in service.events:
            _validate_identifier(event.source, event.name, "event name")
            if event.name in event_names:
                raise _source_error(event.source, "duplicate event name")
            event_names.add(event.name)

            field_names: set[str] = set()
            for field in event.fields:
                _validate_identifier(field.source, field.name, "field name")
                if field.name in field_names:
                    raise _source_error(field.source, "duplicate field name")
                if field.type_name not in SUPPORTED_FIELD_TYPES:
                    raise _source_error(field.source, f"unsupported field type: {field.type_name}")
                field_names.add(field.name)

            if event.e2e is not None:
                _validate_e2e(event.e2e)


def _validate_e2e(e2e: E2EProtectionModel) -> None:
    if e2e.profile not in E2E_PROFILES:
        raise _source_error(e2e.source, f"unsupported E2E profile: {e2e.profile}")
    if e2e.data_id <= 0 or e2e.data_id > 0xFFFFFFFF:
        raise _source_error(e2e.source, "E2E data_id must fit uint32 and be non-zero")
    if e2e.counter_bits not in E2E_COUNTER_WIDTHS:
        raise _source_error(e2e.source, "E2E counter_bits must be one of 4, 8, 16, 32")

    max_counter = (1 << e2e.counter_bits) - 1
    if e2e.max_delta_counter <= 0 or e2e.max_delta_counter > max_counter:
        raise _source_error(e2e.source, "E2E max_delta_counter is outside counter range")
    if e2e.timeout_ms <= 0:
        raise _source_error(e2e.source, "E2E timeout_ms must be positive")
    if e2e.max_repetitions < 0:
        raise _source_error(e2e.source, "E2E max_repetitions cannot be negative")


def _validate_identifier(source: SourceRef, value: str, label: str) -> None:
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", value) is None:
        raise _source_error(source, f"{label} is not a valid identifier")


def _validate_instance(source: SourceRef, value: str) -> None:
    if not value.startswith("/"):
        raise _source_error(source, "service instance must start with /")
    if "//" in value:
        raise _source_error(source, "service instance contains //")
    if re.fullmatch(r"[A-Za-z0-9_./-]+", value) is None:
        raise _source_error(source, "service instance contains unsupported characters")


def _source_error(source: SourceRef, message: str) -> ModelError:
    return ModelError(Path(source.path), source.pointer, message)


def _required_string(path: Path, data: dict[str, Any], key: str, pointer: str) -> str:
    value = data.get(key)
    if not isinstance(value, str) or not value:
        raise ModelError(path, pointer, "value must be a non-empty string")
    return value


def _required_int(path: Path, data: dict[str, Any], key: str, pointer: str) -> int:
    value = data.get(key)
    if not isinstance(value, int) or isinstance(value, bool):
        raise ModelError(path, pointer, "value must be an integer")
    return value


def _optional_int(
    path: Path,
    data: dict[str, Any],
    key: str,
    default: int,
    pointer: str,
) -> int:
    if key not in data:
        return default
    return _required_int(path, data, key, pointer)


def _source_ref(root: Path, path: Path, pointer: str) -> SourceRef:
    return SourceRef(path=path.relative_to(root).as_posix(), pointer=pointer)


def _local_name(tag: str) -> str:
    if "}" in tag:
        return tag.rsplit("}", maxsplit=1)[1]
    return tag


def _child_text(node: ET.Element, child_name: str) -> str:
    for child in node:
        if _local_name(child.tag) == child_name and child.text:
            return child.text.strip()
    return ""


def _direct_descendants(node: ET.Element, child_name: str) -> list[ET.Element]:
    matches: list[ET.Element] = []
    for child in node:
        if _local_name(child.tag) == child_name:
            matches.append(child)
        matches.extend(_direct_descendants(child, child_name))
    return matches


def _xml_pointer(node: ET.Element) -> str:
    name = _child_text(node, "SHORT-NAME")
    suffix = f"[{name}]" if name else ""
    return f"/{_local_name(node.tag)}{suffix}"


def _last_ref_segment(value: str) -> str:
    if not value:
        return ""
    return value.rsplit("/", maxsplit=1)[-1]


def _service_manifest(service: ServiceModel) -> dict[str, Any]:
    return {
        "events": [_event_manifest(event) for event in service.events],
        "instance": service.instance,
        "name": service.name,
        "source": {"path": service.source.path, "pointer": service.source.pointer},
    }


def _event_manifest(event: EventModel) -> dict[str, Any]:
    result: dict[str, Any] = {
        "fields": [{"name": field.name, "type": field.type_name} for field in event.fields],
        "name": event.name,
        "source": {"path": event.source.path, "pointer": event.source.pointer},
    }
    if event.e2e is not None:
        result["e2e"] = {
            "counter_bits": event.e2e.counter_bits,
            "data_id": event.e2e.data_id,
            "max_delta_counter": event.e2e.max_delta_counter,
            "max_repetitions": event.e2e.max_repetitions,
            "profile": event.e2e.profile,
            "source": {
                "path": event.e2e.source.path,
                "pointer": event.e2e.source.pointer,
            },
            "timeout_ms": event.e2e.timeout_ms,
        }
    return result


def _service_summary(service: ServiceModel) -> dict[str, Any]:
    return {
        "e2e_event_count": sum(1 for event in service.events if event.e2e is not None),
        "event_count": len(service.events),
        "events": [
            {
                "e2e_protected": event.e2e is not None,
                "field_count": len(event.fields),
                "name": event.name,
            }
            for event in service.events
        ],
        "instance": service.instance,
        "name": service.name,
        "source": service.source.path,
    }


def _service_header(service: ServiceModel) -> str:
    event_blocks = []
    for event in service.events:
        fields = "\n".join(
            f"  {SUPPORTED_FIELD_TYPES[field.type_name]} {field.name}{{}};"
            for field in event.fields
        )
        block = f"struct {event.name} final {{\n{fields}\n}};\n"
        if event.e2e is not None:
            prefix = f"k{service.name}{event.name}E2E"
            block += (
                f"inline constexpr std::uint32_t {prefix}DataId = "
                f"0x{event.e2e.data_id:08X}U;\n"
                f"inline constexpr std::uint8_t {prefix}CounterBits = "
                f"{event.e2e.counter_bits}U;\n"
                f"inline constexpr std::uint32_t {prefix}MaxDeltaCounter = "
                f"{event.e2e.max_delta_counter}U;\n"
                f"inline constexpr std::uint32_t {prefix}TimeoutMs = "
                f"{event.e2e.timeout_ms}U;\n"
                f"inline constexpr std::uint32_t {prefix}MaxRepetitions = "
                f"{event.e2e.max_repetitions}U;\n"
                f"inline constexpr std::string_view {prefix}Profile = "
                f"\"{event.e2e.profile}\";\n"
            )
        event_blocks.append(block)

    return (
        "// SPDX-License-Identifier: MIT\n"
        "// Generated by oa-cli; do not edit manually.\n"
        "\n"
        "#pragma once\n"
        "\n"
        "#include <cstdint>\n"
        "#include <string_view>\n"
        "\n"
        "namespace openautosar::generated {\n"
        "\n"
        f"inline constexpr std::string_view k{service.name}Name =\n"
        f"  \"{service.name}\";\n"
        f"inline constexpr std::string_view k{service.name}Instance =\n"
        f"  \"{service.instance}\";\n"
        "\n"
        + "\n".join(event_blocks)
        + "}  // namespace openautosar::generated\n"
    )


def _traceability_records(
    service: ServiceModel,
    manifest_path: str,
    header_path: str,
) -> list[dict[str, str]]:
    records = [
        {
            "artifact": manifest_path,
            "element": f"service:{service.name}",
            "source_path": service.source.path,
            "source_pointer": service.source.pointer,
        },
        {
            "artifact": header_path,
            "element": f"service:{service.name}",
            "source_path": service.source.path,
            "source_pointer": service.source.pointer,
        },
    ]
    for event in service.events:
        records.append(
            {
                "artifact": header_path,
                "element": f"event:{service.name}.{event.name}",
                "source_path": event.source.path,
                "source_pointer": event.source.pointer,
            }
        )
        if event.e2e is not None:
            records.append(
                {
                    "artifact": manifest_path,
                    "element": f"e2e:{service.name}.{event.name}",
                    "source_path": event.e2e.source.path,
                    "source_pointer": event.e2e.source.pointer,
                }
            )
            records.append(
                {
                    "artifact": header_path,
                    "element": f"e2e:{service.name}.{event.name}",
                    "source_path": event.e2e.source.path,
                    "source_pointer": event.e2e.source.pointer,
                }
            )
        for field in event.fields:
            records.append(
                {
                    "artifact": header_path,
                    "element": f"field:{service.name}.{event.name}.{field.name}",
                    "source_path": field.source.path,
                    "source_pointer": field.source.pointer,
                }
            )
    return records


def _snake_case(value: str) -> str:
    first_pass = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", value)
    second_pass = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", first_pass)
    return second_pass.lower()


def _write_json(data: dict[str, Any], output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")
