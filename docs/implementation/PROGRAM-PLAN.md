<!-- SPDX-License-Identifier: MIT -->

# OpenAUTOSAR Program Plan

Status: provisional until interview round 4 closes the host, diagnostic,
interoperability, dashboard, and security questions.

## Confirmed Scope

The first executable product increment is a QEMU-only R22-11 vertical slice.
It demonstrates legal controls, model-driven generation, lifecycle, service
communication, Classic simulation, diagnostics, update behavior, dashboard
visibility, and OEM evidence export. Physical boards, MCU scope, production
conformance claims, and proprietary Bosch or ETAS contracts are excluded until
authorized source material and project agreements exist.

## Product Variants

- `generic-oem`: default package and OEM evidence export.
- `generic-supplier`: neutral supplier delivery bundle layout.
- `bosch-supplier`: generic Bosch-style adapter without confidential workflow.
- `qemu-x86_64-dev`: host and QEMU-oriented development preset.
- `host-release`: local release verification preset.

## Target And Baseline

The current target is `qemu-x86_64` using the `qemux86-64` AGL machine shape.
The Linux baseline is AGL Ultimate Unagi 21.0.2 with Yocto Scarthgap 5.0.18.
Future hardware is deliberately deferred; all near-term device, network, CAN,
storage, and update behaviors are simulated in the virtual target.

## Make Buy Reuse

| Area | Decision | Evidence |
|---|---|---|
| Adaptive runtime APIs | Make original subset | `bsw/` libraries |
| SOME/IP | Make original UDP-first implementation | SOME/IP unit tests |
| DDS/RTPS | Make original subset | DDS and RTPS unit tests |
| Linux distribution | Reuse AGL and Yocto | `meta-openautosar` |
| Update backend | Reuse replaceable RAUC backend | UCM policies |
| Dashboard | Reuse Qt 6 and QML | dashboard package |
| CI | Reuse Jenkins | shared CI library |
| Source references | Reuse official public indexes only | source baseline |

## Requirement Matrix

| Requirement group | Controlled artifact | Validation |
|---|---|---|
| Legal and compliance | `compliance/legal` | `validate-compliance.sh` |
| R22-11 source baseline | `requirements/autosar` | `validate-program-governance.sh` |
| Model and traceability | `model/examples/vehicle` | `generate.sh` |
| Architecture work products | `security`, `safety`, `docs/api` | architecture validators |
| ADRs | `docs/decisions` | `validate-architecture-decisions.sh` |
| Quality and performance | `requirements/quality` | `validate-quality-metrics.sh` |
| Classic integration | `integration/virtual-vehicle` | `validate-classic-integration.sh` |
| OEM delivery | `profiles` and OEM export | delivery validators |

## Architecture Decisions

Frozen decisions are recorded as ADRs under `docs/decisions`. Open decisions
remain in `docs/decisions/open-decision-register.yaml` and must be closed
through ADRs only after the relevant round-4 answers are available.

## Team Ownership

The capacity baseline is one primary developer at 10 hours per week. Only one
major workstream is active at a time until named contributors are available.
The first safe split for additional contributors is platform, generators,
lifecycle, SOME/IP, DDS/RTPS, PHM diagnostics update, dashboard tooling, and
safety security evidence.

## 30 Day Plan

- Keep host build, unit tests, license scan, and Jenkins contract green.
- Maintain legal, source-baseline, ADR, and governance evidence.
- Preserve QEMU-only target assumptions in package and release evidence.
- Keep generated artifacts deterministic and marked as generated.

## 60 Day Plan

- Strengthen the QEMU network-lab smoke path and captured evidence.
- Expand model and generated manifest validation around the ultrasonic slice.
- Tighten dashboard status coverage for process, state, service, and health.
- Keep OEM export and supplier delivery reproducible from the package tree.

## 90 Day Plan

- Drive the vertical slice through lifecycle, local IPC, SOME/IP UDP, and vcan.
- Add stronger diagnostic, PHM, and update rollback evidence.
- Record any closed round-4 answers as ADRs or open-decision updates.
- Preserve generic Bosch-style delivery without proprietary claims.

## Milestones

| Horizon | Target outcome |
|---|---|
| 6 months | Reproducible QEMU lifecycle and ultrasonic dashboard slice. |
| 12 months | Credible local IPC and SOME/IP UDP vertical slice. |
| 24 months | Broader DDS, diagnostics, update, and OEM evidence baseline. |

## Risk Register

| Risk | Mitigation |
|---|---|
| AUTOSAR IP boundary breach | Commit only project-owned artifacts and public indexes. |
| AGL or Yocto cache cost | Keep QEMU prebuilt and smoke paths before custom images. |
| Part-time capacity | Enforce one major active workstream and small exit evidence. |
| Protocol undercoverage | Require vectors, packet evidence, and external peer plans. |
| Security key model unknown | Keep signing provider replaceable and decision open. |
| Dashboard license path | Track LGPL or commercial decision before production image. |
| Bosch-specific assumptions | Keep profile generic until lawful project contracts exist. |

## Staffing And Budget

Planning assumes about 40 focused engineering hours per month from one primary
developer. The broad reference release remains a multi-year effort at that
capacity. A production-grade platform needs dedicated owners for platform,
AUTOSAR methodology, protocols, safety, cybersecurity, CI, tooling, release,
and OEM integration.

## OEM Bosch Integration

The active integration path is OEM-neutral. Bosch-style outputs may include
layout, naming, quality gates, and report placeholders, but they must not imply
confidential Bosch approval, proprietary interfaces, or contract-specific
workflow until the project has lawful authorization.

## Acceptance Criteria

- `scripts/test.sh`, `scripts/release.sh`, and `scripts/package.sh` pass.
- Jenkins shared-library contract contains all authoritative validation gates.
- OEM export manifests are valid with zero missing optional evidence.
- Supplier delivery manifests are valid and content-addressed.
- Generated code is byte-stable and explicitly marked as generated.
- Governance, ADR, source-baseline, and workstream validators pass.

## Release Roadmap

1. Maintain the QEMU-only governance and legal baseline.
2. Keep AGL, Yocto, Jenkins, and packaging reproducible.
3. Complete the ultrasonic model, lifecycle, and dashboard slice.
4. Expand local IPC and SOME/IP UDP evidence before TCP.
5. Add DDS/RTPS subset evidence for the same generated contract.
6. Expand PHM, persistency, diagnostics, and update rollback evidence.
7. Regenerate complete OEM and supplier delivery bundles from a clean checkout.
