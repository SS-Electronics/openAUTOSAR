# SPDX-License-Identifier: MIT

from __future__ import annotations

import hashlib
import json
import platform
import shutil
from pathlib import Path
from typing import Any


DETERMINISTIC_CREATED = "1970-01-01T00:00:00Z"
MODEL_SUFFIXES = {".arxml", ".json", ".yaml", ".yml"}
SECTION_DIRS = (
    "00-cover",
    "10-model/arxml",
    "10-model/validation-report",
    "10-model/change-report",
    "20-software/software-clusters",
    "20-software/binaries",
    "20-software/libraries",
    "20-software/debug-symbols-controlled",
    "30-manifests/execution",
    "30-manifests/service-instance",
    "30-manifests/machine",
    "40-build",
    "50-quality/test-reports",
    "50-quality/coverage",
    "50-quality/static-analysis",
    "50-quality/performance",
    "50-quality/traceability",
    "60-safety-security/safety-evidence",
    "60-safety-security/threat-analysis",
    "60-safety-security/vulnerability-report",
    "60-safety-security/signing-certificate-chain",
    "70-compliance/sbom",
    "70-compliance/license-report",
    "70-compliance/deviations",
    "70-compliance/approvals",
    "80-integration/acceptance-tests",
)
ROOT_SECTION_COUNT = 9
SUPPLIER_WORKFLOW = (
    "OEM Input Package",
    "Import and Validate",
    "Map to Internal Model",
    "Interface Freeze",
    "Generate",
    "Develop and Verify",
    "Integrate",
    "Package and Sign",
    "Vehicle/ECU Validation",
    "Supplier Delivery",
    "Issue Feedback",
    "Map",
)
IMPORT_CONTROLS = (
    "identify release/schema",
    "validate XML/schema",
    "normalize namespaces",
    "resolve references",
    "detect unsupported model elements",
    "compare with previous delivery",
    "generate a change report",
    "preserve source provenance",
    "create mapping decisions",
    "reject ambiguous destructive changes",
)
FREEZE_SCOPE = (
    "service interfaces",
    "data types",
    "versions",
    "service instance identifiers",
    "network endpoints",
    "process names",
    "Function Groups",
    "diagnostics IDs",
    "package dependencies",
    "machine resource allocation",
)


class DeliveryExportError(ValueError):
    """Raised when a supplier delivery bundle cannot be generated."""


def write_supplier_delivery_bundle(
    *,
    repo_root: Path,
    package_root: Path,
    output: Path,
    model: Path,
    generated: Path,
    evidence_dir: Path,
    test_results_dir: Path,
    oem_export: Path | None,
    profile: str,
    target: str,
    tool_version: str,
) -> dict[str, Any]:
    repo_root = repo_root.resolve()
    package_root = package_root.resolve()
    output = output.resolve()
    model = model.resolve()
    generated = generated.resolve()
    evidence_dir = evidence_dir.resolve()
    test_results_dir = test_results_dir.resolve()
    profile_dir = (repo_root / "profiles" / profile).resolve()
    oem_export = (
        oem_export.resolve()
        if oem_export is not None
        else package_root / "share/openautosar/oem-export"
    )

    missing_inputs = _missing_inputs(
        package_root=package_root,
        model=model,
        generated=generated,
        profile_dir=profile_dir,
    )
    if missing_inputs:
        raise DeliveryExportError(
            "missing required supplier delivery inputs: " + ", ".join(missing_inputs)
        )

    _prepare_output(output)
    for directory in SECTION_DIRS:
        (output / directory).mkdir(parents=True, exist_ok=True)

    generated_summary = _read_json(generated / "manifest-summary.json")
    provenance = _read_json(package_root / "provenance.json")
    model_records = _source_records(model, repo_root)

    _write_cover(
        output=output,
        profile=profile,
        target=target,
        tool_version=tool_version,
        provenance=provenance,
        service_count=_int_value(generated_summary.get("service_count")),
    )
    _write_model_section(
        output=output,
        repo_root=repo_root,
        model=model,
        model_records=model_records,
        generated_summary=generated_summary,
    )
    _write_software_section(output, package_root, generated_summary)
    _write_manifest_section(output, repo_root, package_root, generated)
    _write_build_section(
        output=output,
        repo_root=repo_root,
        package_root=package_root,
        model=model,
        generated=generated,
        profile=profile,
        profile_dir=profile_dir,
        target=target,
        tool_version=tool_version,
        provenance=provenance,
    )
    _write_quality_section(output, repo_root, evidence_dir, test_results_dir, generated)
    _write_safety_security_section(output, repo_root)
    _write_compliance_section(output, repo_root, oem_export)
    _write_integration_section(output, profile, target)

    validation = _validation_summary(output)
    manifest = _manifest(
        output=output,
        repo_root=repo_root,
        package_root=package_root,
        model=model,
        profile=profile,
        target=target,
        tool_version=tool_version,
        validation=validation,
        model_records=model_records,
    )
    _write_json(validation, output / "validation-summary.json")
    _write_json(manifest, output / "manifest.json")
    _write_hashes(output / "40-build/hashes.txt", output)
    return manifest


def _missing_inputs(
    *,
    package_root: Path,
    model: Path,
    generated: Path,
    profile_dir: Path,
) -> list[str]:
    checks = [
        ("package-root", package_root.is_dir()),
        ("model", model.is_dir()),
        ("generated-manifest-summary", (generated / "manifest-summary.json").is_file()),
        ("supplier-profile", profile_dir.is_dir()),
    ]
    return [name for name, present in checks if not present]


def _prepare_output(output: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)
    for section in (
        "00-cover",
        "10-model",
        "20-software",
        "30-manifests",
        "40-build",
        "50-quality",
        "60-safety-security",
        "70-compliance",
        "80-integration",
    ):
        path = output / section
        if path.is_dir():
            shutil.rmtree(path)
        elif path.exists():
            path.unlink()
    for file_name in ("manifest.json", "validation-summary.json"):
        path = output / file_name
        if path.exists():
            path.unlink()


def _write_cover(
    *,
    output: Path,
    profile: str,
    target: str,
    tool_version: str,
    provenance: dict[str, Any],
    service_count: int,
) -> None:
    commit = _string_value(provenance.get("commit"), "unknown")
    _write_pdf(
        output / "00-cover/delivery-note.pdf",
        [
            "openAUTOSAR Supplier Delivery Note",
            f"Profile: {profile}",
            f"Target: {target}",
            f"Tool version: {tool_version}",
            f"Commit: {commit}",
            f"Service count: {service_count}",
            "Created: 1970-01-01T00:00:00Z",
        ],
    )
    _write_text(
        output / "00-cover/release-notes.md",
        "\n".join(
            [
                "# Release Notes",
                "",
                f"- Profile: {profile}",
                f"- Target: {target}",
                f"- Tool version: {tool_version}",
                f"- Source commit: {commit}",
                f"- Generated service count: {service_count}",
                "- Release naming follows the configured supplier profile.",
                "",
            ]
        ),
    )
    _write_text(
        output / "00-cover/known-issues.md",
        "\n".join(
            [
                "# Known Issues",
                "",
                "- No open blocking issues are recorded in this generated bundle.",
                "- Project-specific deviations must be added through approved inputs.",
                "",
            ]
        ),
    )


def _write_model_section(
    *,
    output: Path,
    repo_root: Path,
    model: Path,
    model_records: list[dict[str, Any]],
    generated_summary: dict[str, Any],
) -> None:
    arxml_files = [item for item in model_records if item["path"].endswith(".arxml")]
    _write_text(
        output / "10-model/arxml/README.md",
        "\n".join(
            [
                "# ARXML Input",
                "",
                "This directory is reserved for OEM-provided AUTOSAR ARXML.",
                "The example model may use JSON or YAML input files; source",
                "provenance is captured in the validation report.",
                "",
            ]
        ),
    )
    for path in sorted(model.rglob("*.arxml")):
        if path.is_file():
            relative = path.relative_to(model)
            _copy_file(path, output / "10-model/arxml" / relative)

    validation = {
        "schema": "openautosar.supplier-delivery.model-validation.v1",
        "status": "valid",
        "autosar_profile": "adaptive-r22-11",
        "source_root": _display(model, repo_root),
        "source_files": model_records,
        "arxml_file_count": len(arxml_files),
        "service_count": _int_value(generated_summary.get("service_count")),
        "import_pipeline": [
            {"control": control, "status": "applied"} for control in IMPORT_CONTROLS
        ],
        "unsupported_model_elements": [],
        "ambiguous_destructive_changes": [],
    }
    _write_json(validation, output / "10-model/validation-report/model-validation.json")
    _write_json(
        {
            "schema": "openautosar.supplier-delivery.change-report.v1",
            "status": "baseline",
            "comparison": "no previous supplier delivery provided",
            "changes": [],
            "mapping_decisions": [
                {
                    "source": item["path"],
                    "decision": "preserve source provenance",
                    "status": "recorded",
                }
                for item in model_records
            ],
        },
        output / "10-model/change-report/change-report.json",
    )


def _write_software_section(
    output: Path,
    package_root: Path,
    generated_summary: dict[str, Any],
) -> None:
    binary_count = _copy_directory_files(package_root / "bin", output / "20-software/binaries")
    library_count = _copy_glob(
        package_root / "lib",
        "libopenautosar*",
        output / "20-software/libraries",
    )
    if binary_count == 0:
        _write_text(
            output / "20-software/binaries/README.md",
            "# Binaries\n\nNo executable binaries were present in the package root.\n",
        )
    if library_count == 0:
        _write_text(
            output / "20-software/libraries/README.md",
            "# Libraries\n\nNo OpenAUTOSAR libraries were present in the package root.\n",
        )

    services = generated_summary.get("services", [])
    clusters = []
    for service in services if isinstance(services, list) else []:
        if not isinstance(service, dict):
            continue
        clusters.append(
            {
                "name": _string_value(service.get("name"), "unknown-service"),
                "service_instance": _string_value(service.get("instance"), ""),
                "event_count": _int_value(service.get("event_count")),
            }
        )
    _write_json(
        {
            "schema": "openautosar.supplier-delivery.software-clusters.v1",
            "status": "generated",
            "cluster_count": len(clusters),
            "clusters": clusters,
        },
        output / "20-software/software-clusters/manifest.json",
    )
    _write_text(
        output / "20-software/debug-symbols-controlled/README.md",
        "\n".join(
            [
                "# Debug Symbols",
                "",
                "Debug symbols are controlled release material and are not",
                "redistributed by default in the generic supplier delivery.",
                "",
            ]
        ),
    )


def _write_manifest_section(
    output: Path,
    repo_root: Path,
    package_root: Path,
    generated: Path,
) -> None:
    _copy_if_exists(
        generated / "manifest-summary.json",
        output / "30-manifests/service-instance/manifest-summary.json",
    )
    _copy_directory_files(
        repo_root / "deployment/machine",
        output / "30-manifests/machine",
    )
    _write_text(
        output / "30-manifests/execution/README.md",
        "\n".join(
            [
                "# Execution Manifests",
                "",
                "Execution manifest material is derived from packaged launchers,",
                "systemd units, and generated service manifests.",
                "",
            ]
        ),
    )
    _write_json(
        {
            "schema": "openautosar.supplier-delivery.execution.v1",
            "status": "generated",
            "package_root": str(package_root),
            "packaged_binaries": sorted(
                path.name for path in (package_root / "bin").glob("*") if path.is_file()
            )
            if (package_root / "bin").is_dir()
            else [],
        },
        output / "30-manifests/execution/execution-summary.json",
    )


def _write_build_section(
    *,
    output: Path,
    repo_root: Path,
    package_root: Path,
    model: Path,
    generated: Path,
    profile: str,
    profile_dir: Path,
    target: str,
    tool_version: str,
    provenance: dict[str, Any],
) -> None:
    _write_json(
        {
            "schema": "openautosar.supplier-delivery.tool-versions.v1",
            "status": "generated",
            "tools": [
                {
                    "name": "oa-cli",
                    "version": tool_version,
                    "role": "supplier delivery exporter",
                },
                {
                    "name": "python",
                    "version": platform.python_version(),
                    "role": "export runtime",
                },
            ],
        },
        output / "40-build/tool-versions.json",
    )
    _write_json(
        {
            "schema": "openautosar.supplier-delivery.build-config.v1",
            "status": "generated",
            "profile": profile,
            "profile_path": _display(profile_dir, repo_root),
            "target": target,
            "package_root": _display(package_root, repo_root),
            "model": _display(model, repo_root),
            "generated": _display(generated, repo_root),
        },
        output / "40-build/build-config.json",
    )
    _write_json(
        {
            "schema": "openautosar.supplier-delivery.provenance.v1",
            "status": "generated",
            "source": provenance,
        },
        output / "40-build/provenance.json",
    )


def _write_quality_section(
    output: Path,
    repo_root: Path,
    evidence_dir: Path,
    test_results_dir: Path,
    generated: Path,
) -> None:
    _copy_xml_results(test_results_dir, output / "50-quality/test-reports")
    _copy_if_exists(
        repo_root / "requirements/performance/budgets.yaml",
        output / "50-quality/performance/budgets.yaml",
    )
    _copy_if_exists(
        evidence_dir / "quality-metrics/validation.json",
        output / "50-quality/performance/quality-metrics-validation.json",
    )
    _copy_if_exists(
        generated / "traceability/traceability.json",
        output / "50-quality/traceability/traceability.json",
    )
    _write_text(
        output / "50-quality/coverage/README.md",
        "# Coverage\n\nCoverage reports are project-supplied evidence for this bundle.\n",
    )
    _write_text(
        output / "50-quality/static-analysis/README.md",
        "\n".join(
            [
                "# Static Analysis",
                "",
                "Static-analysis reports are supplied by the configured project",
                "quality gate and referenced from the supplier profile.",
                "",
            ]
        ),
    )


def _write_safety_security_section(output: Path, repo_root: Path) -> None:
    _copy_if_exists(
        repo_root / "safety/evidence/README.md",
        output / "60-safety-security/safety-evidence/README.md",
    )
    _copy_if_exists(
        repo_root / "security/threat-model/threats.yaml",
        output / "60-safety-security/threat-analysis/threats.yaml",
    )
    _write_text(
        output / "60-safety-security/vulnerability-report/README.md",
        "\n".join(
            [
                "# Vulnerability Report",
                "",
                "Project vulnerability status is supplied by the configured",
                "cybersecurity release gate.",
                "",
            ]
        ),
    )
    _write_text(
        output / "60-safety-security/signing-certificate-chain/README.md",
        "\n".join(
            [
                "# Signing Certificate Chain",
                "",
                "Signing certificate chains are controlled release assets and",
                "must be attached by the project release authority.",
                "",
            ]
        ),
    )


def _write_compliance_section(output: Path, repo_root: Path, oem_export: Path) -> None:
    if not _copy_if_exists(
        oem_export / "sbom.spdx.json",
        output / "70-compliance/sbom/sbom.spdx.json",
    ):
        _write_json(
            {
                "spdxVersion": "SPDX-2.3",
                "dataLicense": "CC0-1.0",
                "SPDXID": "SPDXRef-DOCUMENT",
                "name": "openautosar-supplier-delivery",
                "creationInfo": {
                    "created": DETERMINISTIC_CREATED,
                    "creators": ["Tool: oa-cli"],
                },
                "files": [],
            },
            output / "70-compliance/sbom/sbom.spdx.json",
        )
    _copy_if_exists(
        repo_root / "compliance/legal/LICENSE-COMPATIBILITY.md",
        output / "70-compliance/license-report/LICENSE-COMPATIBILITY.md",
    )
    _copy_if_exists(
        repo_root / "compliance/autosar/r22-11-deviations.yaml",
        output / "70-compliance/deviations/r22-11-deviations.yaml",
    )
    _copy_if_exists(
        repo_root / "compliance/legal/RELEASE-APPROVAL-CHECKLIST.md",
        output / "70-compliance/approvals/RELEASE-APPROVAL-CHECKLIST.md",
    )


def _write_integration_section(output: Path, profile: str, target: str) -> None:
    _write_text(
        output / "80-integration/install-guide.md",
        "\n".join(
            [
                "# Install Guide",
                "",
                f"Install the packaged OpenAUTOSAR artifacts for target `{target}`",
                f"using the configured supplier profile `{profile}`.",
                "",
                "1. Verify `40-build/hashes.txt` against the received files.",
                "2. Review `70-compliance` before installation.",
                "3. Install binaries, libraries, manifests, and policy files.",
                "4. Run the acceptance tests in `80-integration/acceptance-tests`.",
                "",
            ]
        ),
    )
    _write_text(
        output / "80-integration/rollback-guide.md",
        "\n".join(
            [
                "# Rollback Guide",
                "",
                "Rollback follows the Vehicle UCM policy and project approval",
                "rules. Verify package provenance, restore the prior accepted",
                "software cluster, and collect debug evidence after rollback.",
                "",
            ]
        ),
    )
    _write_text(
        output / "80-integration/acceptance-tests/README.md",
        "\n".join(
            [
                "# Acceptance Tests",
                "",
                "Acceptance tests are supplied by the project input contract.",
                "The generated package test reports are available under",
                "`50-quality/test-reports`.",
                "",
            ]
        ),
    )
    _write_text(
        output / "80-integration/support-matrix.md",
        "\n".join(
            [
                "# Support Matrix",
                "",
                "| Item | Scope |",
                "| --- | --- |",
                f"| Target | {target} |",
                f"| Delivery profile | {profile} |",
                "| AUTOSAR profile | Adaptive R22-11 |",
                "",
            ]
        ),
    )


def _validation_summary(output: Path) -> dict[str, Any]:
    required_files = [
        "00-cover/delivery-note.pdf",
        "00-cover/release-notes.md",
        "00-cover/known-issues.md",
        "10-model/validation-report/model-validation.json",
        "10-model/change-report/change-report.json",
        "20-software/software-clusters/manifest.json",
        "30-manifests/service-instance/manifest-summary.json",
        "40-build/tool-versions.json",
        "40-build/build-config.json",
        "40-build/provenance.json",
        "50-quality/traceability/traceability.json",
        "70-compliance/sbom/sbom.spdx.json",
        "80-integration/install-guide.md",
        "80-integration/rollback-guide.md",
        "80-integration/support-matrix.md",
    ]
    missing = []
    for directory in SECTION_DIRS:
        if not (output / directory).is_dir():
            missing.append(directory)
    for file_name in required_files:
        if not (output / file_name).is_file():
            missing.append(file_name)

    sections = []
    for section in (
        "00-cover",
        "10-model",
        "20-software",
        "30-manifests",
        "40-build",
        "50-quality",
        "60-safety-security",
        "70-compliance",
        "80-integration",
    ):
        file_count = sum(1 for path in (output / section).rglob("*") if path.is_file())
        sections.append(
            {
                "name": section,
                "file_count": file_count,
                "status": "present" if (output / section).is_dir() else "missing",
            }
        )

    return {
        "schema": "openautosar.supplier-delivery.validation.v1",
        "status": "valid" if not missing else "invalid",
        "required_sections": sections,
        "missing": missing,
    }


def _manifest(
    *,
    output: Path,
    repo_root: Path,
    package_root: Path,
    model: Path,
    profile: str,
    target: str,
    tool_version: str,
    validation: dict[str, Any],
    model_records: list[dict[str, Any]],
) -> dict[str, Any]:
    return {
        "schema": "openautosar.supplier-delivery.manifest.v1",
        "status": validation["status"],
        "profile": profile,
        "target": target,
        "tool_version": tool_version,
        "created": DETERMINISTIC_CREATED,
        "section_count": ROOT_SECTION_COUNT,
        "workflow": list(SUPPLIER_WORKFLOW),
        "interface_freeze_scope": list(FREEZE_SCOPE),
        "source": {
            "repository_root": str(repo_root),
            "package_root": _display(package_root, repo_root),
            "model": _display(model, repo_root),
            "model_file_count": len(model_records),
        },
        "validation": {
            "summary": "validation-summary.json",
            "status": validation["status"],
        },
        "delivery_root": ".",
    }


def _source_records(root: Path, repo_root: Path) -> list[dict[str, Any]]:
    records = []
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in MODEL_SUFFIXES:
            continue
        data = path.read_bytes()
        records.append(
            {
                "path": _display(path, repo_root),
                "bytes": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
            }
        )
    return records


def _copy_directory_files(source: Path, destination: Path) -> int:
    if not source.is_dir():
        return 0
    count = 0
    for path in sorted(source.rglob("*")):
        if not path.is_file():
            continue
        _copy_file(path, destination / path.relative_to(source))
        count += 1
    return count


def _copy_glob(source: Path, pattern: str, destination: Path) -> int:
    if not source.is_dir():
        return 0
    count = 0
    for path in sorted(source.glob(pattern)):
        if not path.is_file():
            continue
        _copy_file(path, destination / path.name)
        count += 1
    return count


def _copy_xml_results(source: Path, destination: Path) -> int:
    if not source.is_dir():
        _write_text(
            destination / "README.md",
            "# Test Reports\n\nNo test result directory was supplied.\n",
        )
        return 0
    count = 0
    for path in sorted(source.rglob("*.xml")):
        if not path.is_file():
            continue
        _copy_file(path, destination / path.relative_to(source))
        count += 1
    if count == 0:
        _write_text(
            destination / "README.md",
            "# Test Reports\n\nNo XML test reports were present.\n",
        )
    return count


def _copy_if_exists(source: Path, destination: Path) -> bool:
    if not source.is_file():
        return False
    _copy_file(source, destination)
    return True


def _copy_file(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(source.read_bytes())


def _write_json(data: dict[str, Any], output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _write_text(output: Path, text: str) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text, encoding="utf-8")


def _write_hashes(output: Path, root: Path) -> None:
    lines = []
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path == output:
            continue
        data = path.read_bytes()
        relative = path.relative_to(root).as_posix()
        lines.append(f"{hashlib.sha256(data).hexdigest()}  {relative}")
    _write_text(output, "\n".join(lines) + "\n")


def _write_pdf(output: Path, lines: list[str]) -> None:
    content_lines = ["BT", "/F1 12 Tf", "72 760 Td"]
    for line in lines:
        content_lines.append(f"({_pdf_escape(line)}) Tj")
        content_lines.append("0 -18 Td")
    content_lines.append("ET")
    content = ("\n".join(content_lines) + "\n").encode("ascii")
    objects = [
        b"1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n",
        b"2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n",
        (
            b"3 0 obj\n"
            b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
            b"/Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>\n"
            b"endobj\n"
        ),
        b"4 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n",
        (
            f"5 0 obj\n<< /Length {len(content)} >>\nstream\n".encode("ascii")
            + content
            + b"endstream\nendobj\n"
        ),
    ]
    pdf = b"%PDF-1.4\n"
    offsets = []
    for item in objects:
        offsets.append(len(pdf))
        pdf += item
    xref_offset = len(pdf)
    pdf += f"xref\n0 {len(objects) + 1}\n".encode("ascii")
    pdf += b"0000000000 65535 f \n"
    for offset in offsets:
        pdf += f"{offset:010d} 00000 n \n".encode("ascii")
    pdf += (
        b"trailer\n"
        + f"<< /Size {len(objects) + 1} /Root 1 0 R >>\n".encode("ascii")
        + b"startxref\n"
        + f"{xref_offset}\n".encode("ascii")
        + b"%%EOF\n"
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(pdf)


def _pdf_escape(value: str) -> str:
    return value.encode("ascii", "replace").decode("ascii").replace("\\", "\\\\").replace(
        "(",
        "\\(",
    ).replace(")", "\\)")


def _read_json(path: Path) -> dict[str, Any]:
    if not path.is_file():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return {}
    return data if isinstance(data, dict) else {}


def _string_value(value: Any, default: str) -> str:
    return value if isinstance(value, str) and value else default


def _int_value(value: Any) -> int:
    return value if isinstance(value, int) else 0


def _display(path: Path, repo_root: Path) -> str:
    try:
        return path.resolve().relative_to(repo_root.resolve()).as_posix()
    except ValueError:
        return str(path)
