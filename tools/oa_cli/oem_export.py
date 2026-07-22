# SPDX-License-Identifier: MIT

from __future__ import annotations

import hashlib
import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any


MODEL_SUFFIXES = {".arxml", ".json", ".yaml", ".yml"}
DETERMINISTIC_CREATED = "1970-01-01T00:00:00Z"


class EvidenceExportError(ValueError):
    """Raised when required OEM export evidence is missing or invalid."""


@dataclass(frozen=True, slots=True)
class ArtifactSpec:
    name: str
    category: str
    path: Path
    required: bool


def write_evidence_bundle(
    *,
    repo_root: Path,
    package_root: Path,
    output: Path,
    model: Path,
    generated: Path,
    evidence_dir: Path,
    test_results_dir: Path,
    profile: str,
    target: str,
    tool_version: str,
    strict: bool = False,
) -> dict[str, Any]:
    repo_root = repo_root.resolve()
    package_root = package_root.resolve()
    output = output.resolve()
    model = model.resolve()
    generated = generated.resolve()
    evidence_dir = evidence_dir.resolve()
    test_results_dir = test_results_dir.resolve()

    specs = _artifact_specs(
        repo_root=repo_root,
        package_root=package_root,
        output=output,
        model=model,
        generated=generated,
        evidence_dir=evidence_dir,
        test_results_dir=test_results_dir,
        profile=profile,
    )
    artifacts: list[dict[str, Any]] = []
    missing_required: list[dict[str, str]] = []
    missing_optional: list[dict[str, str]] = []
    seen: set[Path] = set()

    for spec in specs:
        key = spec.path.resolve()
        if key in seen:
            continue
        seen.add(key)
        if spec.path.is_file():
            artifacts.append(_artifact_record(spec, repo_root))
            continue

        missing = {
            "category": spec.category,
            "name": spec.name,
            "path": _display_path(spec.path, repo_root),
        }
        if spec.required:
            missing_required.append(missing)
        else:
            missing_optional.append(missing)

    if missing_required:
        names = ", ".join(item["name"] for item in missing_required)
        raise EvidenceExportError(f"missing required OEM export artifacts: {names}")
    if strict and missing_optional:
        names = ", ".join(item["name"] for item in missing_optional)
        raise EvidenceExportError(f"missing optional OEM export artifacts in strict mode: {names}")

    artifacts = sorted(artifacts, key=lambda item: (item["category"], item["path"]))
    section_summary = _section_summary(artifacts, missing_optional)
    manifest = {
        "schema": "openautosar.oem-export.manifest.v1",
        "profile": profile,
        "target": target,
        "tool_version": tool_version,
        "repository": {
            "root": str(repo_root),
            "commit": _provenance_commit(package_root),
        },
        "artifact_count": len(artifacts),
        "missing_optional": missing_optional,
        "sections": section_summary,
        "status": "valid",
    }

    output.mkdir(parents=True, exist_ok=True)
    _write_json(manifest, output / "manifest.json")
    _write_json({"artifacts": artifacts, "status": "generated"}, output / "artifact-inventory.json")
    _write_json(_spdx_document(artifacts, profile), output / "sbom.spdx.json")
    _write_json(
        {
            "missing_optional": missing_optional,
            "required_artifacts": "present",
            "status": "valid",
        },
        output / "validation-summary.json",
    )
    return manifest


def _artifact_specs(
    *,
    repo_root: Path,
    package_root: Path,
    output: Path,
    model: Path,
    generated: Path,
    evidence_dir: Path,
    test_results_dir: Path,
    profile: str,
) -> list[ArtifactSpec]:
    specs = [
        ArtifactSpec("license", "source", repo_root / "LICENSE", True),
        ArtifactSpec("notice", "source", repo_root / "NOTICE", False),
        ArtifactSpec(
            "autosar-ip-assessment",
            "legal-compliance",
            repo_root / "compliance/legal/AUTOSAR-IP-ASSESSMENT.md",
            True,
        ),
        ArtifactSpec(
            "branding-guidelines",
            "legal-compliance",
            repo_root / "compliance/legal/BRANDING-GUIDELINES.md",
            True,
        ),
        ArtifactSpec(
            "license-compatibility",
            "legal-compliance",
            repo_root / "compliance/legal/LICENSE-COMPATIBILITY.md",
            True,
        ),
        ArtifactSpec(
            "third-party-notices",
            "legal-compliance",
            repo_root / "compliance/legal/THIRD_PARTY_NOTICES.md",
            True,
        ),
        ArtifactSpec(
            "contributor-ip-policy",
            "legal-compliance",
            repo_root / "compliance/legal/CONTRIBUTOR-IP-POLICY.md",
            True,
        ),
        ArtifactSpec(
            "release-approval-checklist",
            "legal-compliance",
            repo_root / "compliance/legal/RELEASE-APPROVAL-CHECKLIST.md",
            True,
        ),
        ArtifactSpec("provenance", "provenance", package_root / "provenance.json", True),
        ArtifactSpec(
            "cmake-package-config",
            "sdk",
            package_root / "lib/cmake/openautosar/OpenAutosarConfig.cmake",
            True,
        ),
        ArtifactSpec("generated-summary", "generated", generated / "manifest-summary.json", True),
        ArtifactSpec(
            "traceability-map",
            "traceability",
            generated / "traceability/traceability.json",
            True,
        ),
        ArtifactSpec(
            "yocto-layer-validation",
            "quality",
            evidence_dir / "yocto-layer/validation.json",
            False,
        ),
        ArtifactSpec(
            "compliance-validation",
            "quality",
            evidence_dir / "compliance/validation.json",
            False,
        ),
        ArtifactSpec(
            "ci-contract-validation",
            "quality",
            evidence_dir / "ci-contract/validation.json",
            False,
        ),
        ArtifactSpec(
            "architecture-work-products-validation",
            "quality",
            evidence_dir / "architecture-work-products/validation.json",
            False,
        ),
        ArtifactSpec(
            "architecture-decisions-validation",
            "quality",
            evidence_dir / "architecture-decisions/validation.json",
            False,
        ),
        ArtifactSpec(
            "program-governance-validation",
            "quality",
            evidence_dir / "program-governance/validation.json",
            False,
        ),
        ArtifactSpec(
            "implementation-plan-validation",
            "quality",
            evidence_dir / "implementation-plan/validation.json",
            False,
        ),
        ArtifactSpec(
            "tool-confidence-validation",
            "quality",
            evidence_dir / "tool-confidence/validation.json",
            False,
        ),
        ArtifactSpec(
            "api-abi-validation",
            "quality",
            evidence_dir / "api-abi/validation.json",
            False,
        ),
        ArtifactSpec(
            "quality-metrics-validation",
            "quality",
            evidence_dir / "quality-metrics/validation.json",
            False,
        ),
        ArtifactSpec(
            "classic-integration-validation",
            "quality",
            evidence_dir / "classic-integration/validation.json",
            False,
        ),
        ArtifactSpec(
            "delivery-workflow-validation",
            "quality",
            evidence_dir / "delivery-workflow/validation.json",
            False,
        ),
        ArtifactSpec(
            "public-header-baseline",
            "api-abi",
            evidence_dir / "api-abi/public-header-baseline.json",
            False,
        ),
        ArtifactSpec(
            "supplier-delivery-manifest",
            "supplier-delivery",
            repo_root / "out/supplier-delivery" / profile / "manifest.json",
            False,
        ),
        ArtifactSpec(
            "debug-bundle",
            "supportability",
            evidence_dir / "debug-bundle/manifest.json",
            False,
        ),
        ArtifactSpec(
            "qemu-command",
            "virtual-target",
            evidence_dir / "qemu/qemu-command.json",
            False,
        ),
        ArtifactSpec(
            "network-lab-run",
            "protocol-evidence",
            evidence_dir / "network-lab/last-run/run.json",
            False,
        ),
    ]

    for path in sorted(model.rglob("*")) if model.is_dir() else []:
        if path.is_file() and path.suffix.lower() in MODEL_SUFFIXES:
            specs.append(ArtifactSpec(path.name, "model", path, True))

    for path in sorted(package_root.rglob("*")) if package_root.is_dir() else []:
        if path.is_file() and not _is_relative_to(path, output) and not _is_embedded_export(
            path,
            package_root,
        ):
            specs.append(
                ArtifactSpec(path.name, _package_category(path, package_root), path, False)
            )

    for path in sorted(test_results_dir.rglob("*.xml")) if test_results_dir.is_dir() else []:
        specs.append(ArtifactSpec(path.name, "test-results", path, False))

    return specs


def _artifact_record(spec: ArtifactSpec, repo_root: Path) -> dict[str, Any]:
    data = spec.path.read_bytes()
    return {
        "bytes": len(data),
        "category": spec.category,
        "name": spec.name,
        "path": _display_path(spec.path, repo_root),
        "required": spec.required,
        "sha256": hashlib.sha256(data).hexdigest(),
    }


def _section_summary(
    artifacts: list[dict[str, Any]],
    missing_optional: list[dict[str, str]],
) -> list[dict[str, Any]]:
    categories = sorted({artifact["category"] for artifact in artifacts})
    optional_by_category: dict[str, int] = {}
    for missing in missing_optional:
        optional_by_category[missing["category"]] = (
            optional_by_category.get(missing["category"], 0) + 1
        )

    sections = []
    for category in categories:
        count = sum(1 for artifact in artifacts if artifact["category"] == category)
        sections.append(
            {
                "artifact_count": count,
                "missing_optional_count": optional_by_category.get(category, 0),
                "name": category,
                "status": "present",
            }
        )
    for category, missing_count in sorted(optional_by_category.items()):
        if category not in categories:
            sections.append(
                {
                    "artifact_count": 0,
                    "missing_optional_count": missing_count,
                    "name": category,
                    "status": "not-collected",
                }
            )
    return sections


def _spdx_document(artifacts: list[dict[str, Any]], profile: str) -> dict[str, Any]:
    files = []
    for artifact in artifacts:
        files.append(
            {
                "SPDXID": f"SPDXRef-File-{_spdx_id(artifact['path'])}",
                "checksums": [{"algorithm": "SHA256", "checksumValue": artifact["sha256"]}],
                "fileName": artifact["path"],
                "licenseConcluded": "NOASSERTION",
                "licenseInfoInFiles": ["NOASSERTION"],
            }
        )

    return {
        "SPDXID": "SPDXRef-DOCUMENT",
        "creationInfo": {
            "created": DETERMINISTIC_CREATED,
            "creators": ["Tool: oa-cli"],
        },
        "dataLicense": "CC0-1.0",
        "documentNamespace": f"https://openautosar.local/spdx/{_spdx_id(profile)}",
        "files": files,
        "name": f"openautosar-{profile}-oem-export",
        "spdxVersion": "SPDX-2.3",
    }


def _package_category(path: Path, package_root: Path) -> str:
    relative = path.relative_to(package_root).as_posix()
    if relative.startswith("bin/") or relative.startswith("lib/"):
        return "binary"
    if relative.startswith("include/") or relative.startswith("lib/cmake/"):
        return "sdk"
    if relative.startswith("share/openautosar/generated/"):
        return "generated"
    if relative.startswith("share/openautosar/classic-integration/"):
        return "classic-integration"
    if relative.startswith("share/openautosar/yocto/"):
        return "yocto-layer"
    if relative.startswith("share/openautosar/debug-bundle/"):
        return "supportability"
    if relative.startswith("share/openautosar/supplier-delivery/"):
        return "supplier-delivery"
    if relative.startswith("share/openautosar/profiles/"):
        return "delivery-profile"
    if relative.startswith("share/openautosar/work-products/security/"):
        return "security-work-product"
    if relative.startswith("share/openautosar/work-products/safety/"):
        return "safety-work-product"
    if relative.startswith("share/openautosar/work-products/api/"):
        return "api-abi"
    if relative.startswith("share/openautosar/work-products/architecture-decisions/"):
        return "architecture-decision"
    if relative.startswith("share/openautosar/work-products/governance/"):
        return "program-governance"
    if relative.startswith("share/openautosar/work-products/platform-services/"):
        return "platform-service-doc"
    if relative.startswith("share/openautosar/work-products/implementation/"):
        return "implementation-plan"
    if relative.startswith("share/openautosar/work-products/backlog/"):
        return "backlog"
    if relative.startswith("share/openautosar/work-products/planning/"):
        return "implementation-workstream"
    if relative.startswith("share/openautosar/work-products/requirements/autosar/"):
        return "source-baseline"
    if relative.startswith("share/openautosar/work-products/requirements/api/"):
        return "api-abi"
    if relative.startswith("share/openautosar/work-products/tool-confidence/"):
        return "tool-confidence"
    if relative.startswith("share/openautosar/work-products/quality/"):
        return "quality-work-product"
    if relative.startswith("share/openautosar/work-products/verification/"):
        return "verification-work-product"
    if relative.startswith("share/openautosar/work-products/performance/"):
        return "performance-work-product"
    if relative.startswith("share/openautosar/work-products/quality-policy/"):
        return "quality-work-product"
    if relative.startswith("share/openautosar/security/"):
        return "security-policy"
    if relative.startswith("share/openautosar/safety/"):
        return "safety-policy"
    if relative.startswith("share/openautosar/"):
        return "platform-policy"
    return "package"


def _provenance_commit(package_root: Path) -> str:
    provenance = package_root / "provenance.json"
    if not provenance.is_file():
        return "unknown"
    try:
        data = json.loads(provenance.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return "unknown"
    value = data.get("commit")
    return value if isinstance(value, str) and value else "unknown"


def _display_path(path: Path, repo_root: Path) -> str:
    try:
        return path.resolve().relative_to(repo_root).as_posix()
    except ValueError:
        return str(path)


def _is_relative_to(path: Path, parent: Path) -> bool:
    try:
        path.resolve().relative_to(parent.resolve())
    except ValueError:
        return False
    return True


def _is_embedded_export(path: Path, package_root: Path) -> bool:
    try:
        relative = path.resolve().relative_to(package_root.resolve()).as_posix()
    except ValueError:
        return False
    return relative.startswith("share/openautosar/oem-export/")


def _spdx_id(value: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9.-]+", "-", value).strip("-")
    return cleaned or "artifact"


def _write_json(data: dict[str, Any], output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")
