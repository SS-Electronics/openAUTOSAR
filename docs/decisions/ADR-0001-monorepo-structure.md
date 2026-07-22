<!-- SPDX-License-Identifier: MIT -->

# ADR-0001: Monorepo Structure

## Status

Accepted

## Context

The platform combines runtime clusters, virtual-vehicle integration, host tools,
Yocto metadata, verification assets, and release evidence. Splitting these too
early would make cross-cutting build and traceability checks harder to enforce.

## Decision

Keep OpenAUTOSAR in one source-controlled monorepo for the current reference
platform. Component boundaries remain explicit through directories, CMake
targets, package export rules, and validation scripts.

## Consequences

CI can validate the platform as one coherent product baseline. Future extraction
of modules is allowed only after public APIs, package boundaries, and evidence
exports are stable.

## Evidence

- `CMakeLists.txt`
- `scripts/package.sh`
- `tools/oa_cli/oem_export.py`
