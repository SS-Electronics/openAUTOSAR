<!-- SPDX-License-Identifier: MIT -->

# ADR-0014: Complete OEM Artifact Export

## Status

Accepted

## Context

OEM and Tier-1 exchange requires deterministic package contents, evidence
inventories, SBOM material, validation reports, and supportability artifacts.

## Decision

Generate a complete OEM artifact export from the package root, generated model,
test results, validation evidence, supplier delivery output, and legal material.

## Consequences

Release packaging fails when required artifacts are missing, and optional
evidence gaps are reported in the export manifest.

## Evidence

- `tools/oa_cli/oem_export.py`
- `tools/oa_cli/delivery.py`
- `scripts/package.sh`
- `scripts/export-supplier-delivery.sh`
