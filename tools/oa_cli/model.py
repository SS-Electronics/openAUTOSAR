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
class MethodModel:
    name: str
    request_fields: tuple[FieldModel, ...]
    response_fields: tuple[FieldModel, ...]
    fire_and_forget: bool
    errors: tuple["MethodErrorModel", ...]
    source: SourceRef


@dataclass(frozen=True, slots=True)
class MethodErrorModel:
    name: str
    code: int
    description: str
    source: SourceRef


@dataclass(frozen=True, slots=True)
class ServiceFieldModel:
    name: str
    type_name: str
    getter: bool
    setter: bool
    notifier: bool
    source: SourceRef


@dataclass(frozen=True, slots=True)
class ServiceTriggerModel:
    name: str
    source: SourceRef


@dataclass(frozen=True, slots=True)
class ServiceModel:
    name: str
    instance: str
    events: tuple[EventModel, ...]
    methods: tuple[MethodModel, ...]
    service_fields: tuple[ServiceFieldModel, ...]
    triggers: tuple[ServiceTriggerModel, ...]
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
    events_data = data.get("events", [])
    if not isinstance(events_data, list):
        raise ModelError(path, f"{pointer}/events", "service events must be a list")

    methods_data = data.get("methods", [])
    if not isinstance(methods_data, list):
        raise ModelError(path, f"{pointer}/methods", "service methods must be a list")

    fields_data = data.get("fields", [])
    if not isinstance(fields_data, list):
        raise ModelError(path, f"{pointer}/fields", "service fields must be a list")

    triggers_data = data.get("triggers", [])
    if not isinstance(triggers_data, list):
        raise ModelError(path, f"{pointer}/triggers", "service triggers must be a list")

    if not events_data and not methods_data and not fields_data and not triggers_data:
        raise ModelError(
            path,
            pointer,
            "service must define at least one event, method, field, or trigger",
        )

    events: list[EventModel] = []
    for event_index, event_data in enumerate(events_data):
        event_pointer = f"{pointer}/events/{event_index}"
        events.append(_json_event(root, path, event_data, event_pointer))

    methods: list[MethodModel] = []
    for method_index, method_data in enumerate(methods_data):
        method_pointer = f"{pointer}/methods/{method_index}"
        methods.append(_json_method(root, path, method_data, method_pointer))

    service_fields: list[ServiceFieldModel] = []
    for field_index, field_data in enumerate(fields_data):
        field_pointer = f"{pointer}/fields/{field_index}"
        service_fields.append(_json_service_field(root, path, field_data, field_pointer))

    triggers: list[ServiceTriggerModel] = []
    for trigger_index, trigger_data in enumerate(triggers_data):
        trigger_pointer = f"{pointer}/triggers/{trigger_index}"
        triggers.append(_json_service_trigger(root, path, trigger_data, trigger_pointer))

    return ServiceModel(
        name=name,
        instance=instance,
        events=tuple(events),
        methods=tuple(methods),
        service_fields=tuple(service_fields),
        triggers=tuple(triggers),
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


def _json_method(root: Path, path: Path, data: Any, pointer: str) -> MethodModel:
    if not isinstance(data, dict):
        raise ModelError(path, pointer, "method entry must be an object")

    name = _required_string(path, data, "name", f"{pointer}/name")
    request_fields = _json_field_list(
        root,
        path,
        data.get("request", []),
        f"{pointer}/request",
        "method request",
        False,
    )
    response_fields = _json_field_list(
        root,
        path,
        data.get("response", []),
        f"{pointer}/response",
        "method response",
        False,
    )
    fire_and_forget = _optional_bool(
        path,
        data,
        "fire_and_forget",
        False,
        f"{pointer}/fire_and_forget",
    )
    errors_data = data.get("errors", [])
    if not isinstance(errors_data, list):
        raise ModelError(path, f"{pointer}/errors", "method errors must be a list")

    errors: list[MethodErrorModel] = []
    for error_index, error_data in enumerate(errors_data):
        error_pointer = f"{pointer}/errors/{error_index}"
        errors.append(_json_method_error(root, path, error_data, error_pointer))

    return MethodModel(
        name=name,
        request_fields=tuple(request_fields),
        response_fields=tuple(response_fields),
        fire_and_forget=fire_and_forget,
        errors=tuple(errors),
        source=_source_ref(root, path, pointer),
    )


def _json_method_error(
    root: Path,
    path: Path,
    data: Any,
    pointer: str,
) -> MethodErrorModel:
    if not isinstance(data, dict):
        raise ModelError(path, pointer, "method error entry must be an object")

    return MethodErrorModel(
        name=_required_string(path, data, "name", f"{pointer}/name"),
        code=_required_int(path, data, "code", f"{pointer}/code"),
        description=_optional_string(path, data, "description", "", f"{pointer}/description"),
        source=_source_ref(root, path, pointer),
    )


def _json_service_field(
    root: Path,
    path: Path,
    data: Any,
    pointer: str,
) -> ServiceFieldModel:
    if not isinstance(data, dict):
        raise ModelError(path, pointer, "service field entry must be an object")

    return ServiceFieldModel(
        name=_required_string(path, data, "name", f"{pointer}/name"),
        type_name=_required_string(path, data, "type", f"{pointer}/type"),
        getter=_optional_bool(path, data, "getter", True, f"{pointer}/getter"),
        setter=_optional_bool(path, data, "setter", True, f"{pointer}/setter"),
        notifier=_optional_bool(path, data, "notifier", True, f"{pointer}/notifier"),
        source=_source_ref(root, path, pointer),
    )


def _json_service_trigger(
    root: Path,
    path: Path,
    data: Any,
    pointer: str,
) -> ServiceTriggerModel:
    if not isinstance(data, dict):
        raise ModelError(path, pointer, "service trigger entry must be an object")

    return ServiceTriggerModel(
        name=_required_string(path, data, "name", f"{pointer}/name"),
        source=_source_ref(root, path, pointer),
    )


def _json_field_list(
    root: Path,
    path: Path,
    data: Any,
    pointer: str,
    label: str,
    require_non_empty: bool,
) -> list[FieldModel]:
    if not isinstance(data, list) or (require_non_empty and not data):
        raise ModelError(path, pointer, f"{label} must define a field list")

    fields: list[FieldModel] = []
    for field_index, field_data in enumerate(data):
        field_pointer = f"{pointer}/{field_index}"
        fields.append(_json_field(root, path, field_data, field_pointer))
    return fields


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
                methods=tuple(),
                service_fields=tuple(),
                triggers=tuple(),
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

        method_names: set[str] = set()
        for method in service.methods:
            _validate_identifier(method.source, method.name, "method name")
            if method.name in method_names:
                raise _source_error(method.source, "duplicate method name")
            method_names.add(method.name)
            if method.fire_and_forget and method.response_fields:
                raise _source_error(
                    method.source,
                    "fire-and-forget method cannot define response fields",
                )
            if method.fire_and_forget and method.errors:
                raise _source_error(
                    method.source,
                    "fire-and-forget method cannot define errors",
                )
            _validate_method_fields(method.request_fields, "request field")
            _validate_method_fields(method.response_fields, "response field")
            _validate_method_errors(method.errors)

        service_field_names: set[str] = set()
        for field in service.service_fields:
            _validate_identifier(field.source, field.name, "service field name")
            if field.name in service_field_names:
                raise _source_error(field.source, "duplicate service field name")
            if field.type_name not in SUPPORTED_FIELD_TYPES:
                raise _source_error(field.source, f"unsupported field type: {field.type_name}")
            if not field.getter and not field.setter and not field.notifier:
                raise _source_error(
                    field.source,
                    "service field must enable getter, setter, or notifier",
                )
            service_field_names.add(field.name)

        trigger_names: set[str] = set()
        for trigger in service.triggers:
            _validate_identifier(trigger.source, trigger.name, "service trigger name")
            if trigger.name in trigger_names:
                raise _source_error(trigger.source, "duplicate service trigger name")
            trigger_names.add(trigger.name)


def _validate_method_fields(fields: tuple[FieldModel, ...], label: str) -> None:
    field_names: set[str] = set()
    for field in fields:
        _validate_identifier(field.source, field.name, label)
        if field.name in field_names:
            raise _source_error(field.source, f"duplicate {label} name")
        if field.type_name not in SUPPORTED_FIELD_TYPES:
            raise _source_error(field.source, f"unsupported field type: {field.type_name}")
        field_names.add(field.name)


def _validate_method_errors(errors: tuple[MethodErrorModel, ...]) -> None:
    error_names: set[str] = set()
    error_codes: set[int] = set()
    for error in errors:
        _validate_identifier(error.source, error.name, "method error name")
        if error.name in error_names:
            raise _source_error(error.source, "duplicate method error name")
        if error.code in error_codes:
            raise _source_error(error.source, "duplicate method error code")
        if error.code <= 0 or error.code > 0xFFFFFFFF:
            raise _source_error(error.source, "method error code must fit uint32 and be non-zero")
        error_names.add(error.name)
        error_codes.add(error.code)


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


def _optional_string(
    path: Path,
    data: dict[str, Any],
    key: str,
    default: str,
    pointer: str,
) -> str:
    if key not in data:
        return default
    value = data.get(key)
    if not isinstance(value, str):
        raise ModelError(path, pointer, "value must be a string")
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


def _optional_bool(
    path: Path,
    data: dict[str, Any],
    key: str,
    default: bool,
    pointer: str,
) -> bool:
    if key not in data:
        return default
    value = data.get(key)
    if not isinstance(value, bool):
        raise ModelError(path, pointer, "value must be a boolean")
    return value


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
        "fields": [_service_field_manifest(field) for field in service.service_fields],
        "instance": service.instance,
        "methods": [_method_manifest(method) for method in service.methods],
        "name": service.name,
        "source": {"path": service.source.path, "pointer": service.source.pointer},
        "triggers": [_service_trigger_manifest(trigger) for trigger in service.triggers],
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


def _method_manifest(method: MethodModel) -> dict[str, Any]:
    return {
        "errors": [
            {
                "code": error.code,
                "description": error.description,
                "name": error.name,
                "source": {"path": error.source.path, "pointer": error.source.pointer},
            }
            for error in method.errors
        ],
        "fire_and_forget": method.fire_and_forget,
        "name": method.name,
        "request": {
            "fields": [
                {"name": field.name, "type": field.type_name}
                for field in method.request_fields
            ],
        },
        "response": {
            "fields": [
                {"name": field.name, "type": field.type_name}
                for field in method.response_fields
            ],
        },
        "source": {"path": method.source.path, "pointer": method.source.pointer},
    }


def _service_field_manifest(field: ServiceFieldModel) -> dict[str, Any]:
    return {
        "getter": field.getter,
        "name": field.name,
        "notifier": field.notifier,
        "setter": field.setter,
        "source": {"path": field.source.path, "pointer": field.source.pointer},
        "type": field.type_name,
    }


def _service_trigger_manifest(trigger: ServiceTriggerModel) -> dict[str, Any]:
    return {
        "name": trigger.name,
        "source": {"path": trigger.source.path, "pointer": trigger.source.pointer},
    }


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
        "field_count": len(service.service_fields),
        "fields": [
            {
                "getter": field.getter,
                "name": field.name,
                "notifier": field.notifier,
                "setter": field.setter,
                "type": field.type_name,
            }
            for field in service.service_fields
        ],
        "instance": service.instance,
        "method_count": len(service.methods),
        "methods": [
            {
                "fire_and_forget": method.fire_and_forget,
                "error_count": len(method.errors),
                "errors": [
                    {"code": error.code, "name": error.name}
                    for error in method.errors
                ],
                "name": method.name,
                "request_field_count": len(method.request_fields),
                "response_field_count": len(method.response_fields),
            }
            for method in service.methods
        ],
        "name": service.name,
        "source": service.source.path,
        "trigger_count": len(service.triggers),
        "triggers": [{"name": trigger.name} for trigger in service.triggers],
    }


def _service_header(service: ServiceModel) -> str:
    blocks = []
    for event in service.events:
        block = _struct_block(event.name, event.fields)
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
        blocks.append(block)

    for method in service.methods:
        blocks.append(_struct_block(f"{method.name}Request", method.request_fields))
        if not method.fire_and_forget:
            blocks.append(_struct_block(f"{method.name}Response", method.response_fields))
        if method.errors:
            blocks.append(_method_error_enum_block(method))

    for field in service.service_fields:
        blocks.append(_single_field_struct_block(f"{field.name}Field", field))

    event_constants = "".join(
        f"inline constexpr std::string_view k{service.name}{event.name}EventName = "
        f"\"{event.name}\";\n"
        for event in service.events
    )
    method_constants = "".join(
        f"inline constexpr std::string_view k{service.name}{method.name}MethodName = "
        f"\"{method.name}\";\n"
        for method in service.methods
    )
    method_error_constants = "".join(
        f"inline constexpr std::string_view k{service.name}{method.name}ErrorDomain = "
        f"\"{service.name}.{method.name}\";\n"
        for method in service.methods
        if method.errors
    )
    field_constants = "".join(
        f"inline constexpr std::string_view k{service.name}{field.name}FieldName = "
        f"\"{field.name}\";\n"
        for field in service.service_fields
    )
    trigger_constants = "".join(
        f"inline constexpr std::string_view k{service.name}{trigger.name}TriggerName = "
        f"\"{trigger.name}\";\n"
        for trigger in service.triggers
    )
    proxy_block = _proxy_class_block(service)
    skeleton_block = _skeleton_class_block(service)

    return (
        "// SPDX-License-Identifier: MIT\n"
        "// Generated by oa-cli; do not edit manually.\n"
        "\n"
        "#pragma once\n"
        "\n"
        '#include "openautosar/com/service_registry.h"\n'
        '#include "openautosar/core/result.h"\n'
        "\n"
        "#include <cstddef>\n"
        "#include <cstdint>\n"
        "#include <string>\n"
        "#include <string_view>\n"
        "#include <utility>\n"
        "#include <vector>\n"
        "\n"
        "namespace openautosar::generated {\n"
        "\n"
        f"inline constexpr std::string_view k{service.name}Name =\n"
        f"  \"{service.name}\";\n"
        f"inline constexpr std::string_view k{service.name}Instance =\n"
        f"  \"{service.instance}\";\n"
        f"{event_constants}"
        f"{method_constants}"
        f"{method_error_constants}"
        f"{field_constants}"
        f"{trigger_constants}"
        "\n"
        + "\n".join(blocks)
        + proxy_block
        + skeleton_block
        + "}  // namespace openautosar::generated\n"
    )


def _struct_block(name: str, fields: tuple[FieldModel, ...]) -> str:
    if not fields:
        return f"struct {name} final {{}};\n"

    fields_text = "\n".join(
        f"  {SUPPORTED_FIELD_TYPES[field.type_name]} {field.name}{{}};"
        for field in fields
    )
    return f"struct {name} final {{\n{fields_text}\n}};\n"


def _single_field_struct_block(name: str, field: ServiceFieldModel) -> str:
    return (
        f"struct {name} final {{\n"
        f"  {SUPPORTED_FIELD_TYPES[field.type_name]} value{{}};\n"
        "};\n"
    )


def _method_error_enum_block(method: MethodModel) -> str:
    values = "\n".join(
        f"  {error.name} = {error.code}U,"
        for error in method.errors
    )
    return f"enum class {method.name}Error : std::uint32_t {{\n{values}\n}};\n"


def _proxy_class_block(service: ServiceModel) -> str:
    class_name = f"{service.name}Proxy"
    lines = [
        f"class {class_name} final {{",
        "public:",
        f"  {class_name}(",
        "    com::ServiceRegistry& registry,",
        "    com::ServiceIdentifier service) noexcept",
        "      : registry_(registry), service_(service) {}",
        "",
    ]
    for event in service.events:
        lines.extend(
            [
                f"  [[nodiscard]] core::Result<com::Subscription> Subscribe{event.name}(",
                "    std::size_t queue_depth) {",
                "    return registry_.Subscribe(",
                "      service_,",
                f"      std::string(k{service.name}{event.name}EventName),",
                "      queue_depth);",
                "  }",
                "",
                f"  [[nodiscard]] core::Result<com::EventSample> Poll{event.name}(",
                "    std::uint64_t subscription_id) {",
                "    return registry_.Poll(subscription_id);",
                "  }",
                "",
            ]
        )
    for trigger in service.triggers:
        lines.extend(
            [
                f"  [[nodiscard]] core::Result<com::Subscription> Subscribe{trigger.name}Trigger(",
                "    std::size_t queue_depth) {",
                "    return registry_.SubscribeTrigger(",
                "      service_,",
                f"      std::string(k{service.name}{trigger.name}TriggerName),",
                "      queue_depth);",
                "  }",
                "",
                f"  [[nodiscard]] core::Result<com::TriggerActivation> Poll{trigger.name}Trigger(",
                "    std::uint64_t subscription_id) {",
                "    return registry_.PollTrigger(subscription_id);",
                "  }",
                "",
            ]
        )
    for field in service.service_fields:
        if field.getter:
            lines.extend(
                [
                    f"  [[nodiscard]] core::Result<com::FieldValue> Get{field.name}() const {{",
                    "    return registry_.GetField(",
                    "      service_,",
                    f"      std::string(k{service.name}{field.name}FieldName));",
                    "  }",
                    "",
                ]
            )
        if field.setter:
            lines.extend(
                [
                    f"  [[nodiscard]] core::Result<com::FieldValue> Set{field.name}(",
                    "    std::vector<std::uint8_t> payload) {",
                    "    return registry_.SetField({",
                    "      .service = service_,",
                    f"      .field_name = std::string(k{service.name}{field.name}FieldName),",
                    "      .payload = std::move(payload),",
                    "    });",
                    "  }",
                    "",
                ]
            )
        if field.notifier:
            lines.extend(
                [
                    f"  [[nodiscard]] core::Result<com::Subscription> Subscribe{field.name}Field(",
                    "    std::size_t queue_depth) {",
                    "    return registry_.SubscribeField(",
                    "      service_,",
                    f"      std::string(k{service.name}{field.name}FieldName),",
                    "      queue_depth);",
                    "  }",
                    "",
                    f"  [[nodiscard]] core::Result<com::FieldValue> Poll{field.name}Field(",
                    "    std::uint64_t subscription_id) {",
                    "    return registry_.PollField(subscription_id);",
                    "  }",
                    "",
                ]
            )
    for method in service.methods:
        if method.fire_and_forget:
            lines.extend(
                [
                    f"  [[nodiscard]] core::Result<com::MethodCall> FireAndForget{method.name}(",
                    "    std::vector<std::uint8_t> payload,",
                    "    std::uint64_t correlation_id = 0U) {",
                    "    return registry_.SubmitMethodCall({",
                    "      .service = service_,",
                    f"      .method_name = std::string(k{service.name}{method.name}MethodName),",
                    "      .payload = std::move(payload),",
                    "      .correlation_id = correlation_id,",
                    "      .expects_response = false,",
                    "    });",
                    "  }",
                    "",
                ]
            )
        else:
            lines.extend(
                [
                    f"  [[nodiscard]] core::Result<com::MethodCall> Call{method.name}(",
                    "    std::vector<std::uint8_t> payload,",
                    "    std::uint64_t correlation_id = 0U) {",
                    "    return registry_.SubmitMethodCall({",
                    "      .service = service_,",
                    f"      .method_name = std::string(k{service.name}{method.name}MethodName),",
                    "      .payload = std::move(payload),",
                    "      .correlation_id = correlation_id,",
                    "    });",
                    "  }",
                    "",
                    f"  [[nodiscard]] core::Result<com::MethodCallFuture> Call{method.name}Future(",
                    "    std::vector<std::uint8_t> payload,",
                    "    std::uint64_t correlation_id = 0U) {",
                    "    return registry_.SubmitMethodCallFuture({",
                    "      .service = service_,",
                    f"      .method_name = std::string(k{service.name}{method.name}MethodName),",
                    "      .payload = std::move(payload),",
                    "      .correlation_id = correlation_id,",
                    "    });",
                    "  }",
                    "",
                    f"  [[nodiscard]] core::Result<com::MethodResult> Take{method.name}Result(",
                    "    std::uint64_t correlation_id) {",
                    "    return registry_.TakeMethodResult(",
                    "      service_,",
                    f"      std::string(k{service.name}{method.name}MethodName),",
                    "      correlation_id);",
                    "  }",
                    "",
                ]
            )
    lines.extend(
        [
            "private:",
            "  com::ServiceRegistry& registry_;",
            "  com::ServiceIdentifier service_{};",
            "};",
            "",
        ]
    )
    return "\n".join(lines)


def _skeleton_class_block(service: ServiceModel) -> str:
    class_name = f"{service.name}Skeleton"
    lines = [
        f"class {class_name} final {{",
        "public:",
        f"  {class_name}(",
        "    com::ServiceRegistry& registry,",
        "    com::ServiceOffer offer)",
        "      : registry_(registry), offer_(std::move(offer)) {}",
        "",
        "  [[nodiscard]] core::Result<com::ServiceOffer> Offer() {",
        "    return registry_.OfferService(offer_);",
        "  }",
        "",
        "  [[nodiscard]] core::Result<bool> StopOffer() {",
        "    return registry_.StopOffer(offer_.service);",
        "  }",
        "",
    ]
    for event in service.events:
        lines.extend(
            [
                f"  [[nodiscard]] core::Result<std::uint64_t> Publish{event.name}(",
                "    std::vector<std::uint8_t> payload) {",
                "    return registry_.Publish({",
                "      .service = offer_.service,",
                f"      .event_name = std::string(k{service.name}{event.name}EventName),",
                "      .payload = std::move(payload),",
                "    });",
                "  }",
                "",
            ]
        )
    for trigger in service.triggers:
        lines.extend(
            [
                "  [[nodiscard]] core::Result<com::TriggerActivation> "
                f"Fire{trigger.name}Trigger() {{",
                "    return registry_.FireTrigger({",
                "      .service = offer_.service,",
                f"      .trigger_name = std::string(k{service.name}{trigger.name}TriggerName),",
                "    });",
                "  }",
                "",
            ]
        )
    for field in service.service_fields:
        lines.extend(
            [
                f"  [[nodiscard]] core::Result<com::FieldValue> Update{field.name}(",
                "    std::vector<std::uint8_t> payload) {",
                "    return registry_.SetField({",
                "      .service = offer_.service,",
                f"      .field_name = std::string(k{service.name}{field.name}FieldName),",
                "      .payload = std::move(payload),",
                "    });",
                "  }",
                "",
            ]
        )
    for method in service.methods:
        lines.extend(
            [
                f"  [[nodiscard]] core::Result<com::MethodCall> Take{method.name}Call() {{",
                "    return registry_.TakeMethodCall(",
                "      offer_.service,",
                f"      std::string(k{service.name}{method.name}MethodName));",
                "  }",
                "",
            ]
        )
        if not method.fire_and_forget:
            lines.extend(
                [
                    f"  [[nodiscard]] core::Result<com::MethodResult> Complete{method.name}(",
                    "    std::vector<std::uint8_t> payload,",
                    "    std::uint64_t correlation_id,",
                    "    bool application_error = false) {",
                    "    return registry_.CompleteMethodCall({",
                    "      .service = offer_.service,",
                    f"      .method_name = std::string(k{service.name}{method.name}MethodName),",
                    "      .payload = std::move(payload),",
                    "      .correlation_id = correlation_id,",
                    "      .application_error = application_error,",
                    "      .error_domain = {},",
                    "      .error_code = 0U,",
                    "    });",
                    "  }",
                    "",
                ]
            )
            if method.errors:
                lines.extend(
                    [
                        "  [[nodiscard]] core::Result<com::MethodResult> "
                        f"Complete{method.name}Error(",
                        f"    {method.name}Error error,",
                        "    std::vector<std::uint8_t> payload,",
                        "    std::uint64_t correlation_id) {",
                        "    return registry_.CompleteMethodCall({",
                        "      .service = offer_.service,",
                        f"      .method_name = std::string(k{service.name}"
                        f"{method.name}MethodName),",
                        "      .payload = std::move(payload),",
                        "      .correlation_id = correlation_id,",
                        "      .application_error = true,",
                        f"      .error_domain = std::string(k{service.name}"
                        f"{method.name}ErrorDomain),",
                        "      .error_code = static_cast<std::uint32_t>(error),",
                        "    });",
                        "  }",
                        "",
                    ]
                )
    lines.extend(
        [
            "private:",
            "  com::ServiceRegistry& registry_;",
            "  com::ServiceOffer offer_{};",
            "};",
            "",
        ]
    )
    return "\n".join(lines)


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
    for field in service.service_fields:
        records.append(
            {
                "artifact": header_path,
                "element": f"service-field:{service.name}.{field.name}",
                "source_path": field.source.path,
                "source_pointer": field.source.pointer,
            }
        )
    for trigger in service.triggers:
        records.append(
            {
                "artifact": header_path,
                "element": f"trigger:{service.name}.{trigger.name}",
                "source_path": trigger.source.path,
                "source_pointer": trigger.source.pointer,
            }
        )
    for method in service.methods:
        records.append(
            {
                "artifact": header_path,
                "element": f"method:{service.name}.{method.name}",
                "source_path": method.source.path,
                "source_pointer": method.source.pointer,
            }
        )
        for field in method.request_fields:
            records.append(
                {
                    "artifact": header_path,
                    "element": f"request-field:{service.name}.{method.name}.{field.name}",
                    "source_path": field.source.path,
                    "source_pointer": field.source.pointer,
                }
            )
        for field in method.response_fields:
            records.append(
                {
                    "artifact": header_path,
                    "element": f"response-field:{service.name}.{method.name}.{field.name}",
                    "source_path": field.source.path,
                    "source_pointer": field.source.pointer,
                }
            )
        for error in method.errors:
            records.append(
                {
                    "artifact": header_path,
                    "element": f"error:{service.name}.{method.name}.{error.name}",
                    "source_path": error.source.path,
                    "source_pointer": error.source.pointer,
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
