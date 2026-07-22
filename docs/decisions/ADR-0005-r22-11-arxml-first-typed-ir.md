<!-- SPDX-License-Identifier: MIT -->

# ADR-0005: R22-11 ARXML-First Typed IR

## Status

Accepted

## Context

AUTOSAR input must be traceable to controlled model sources while remaining
debuggable and independent from commercial generators.

## Decision

Adopt R22-11 ARXML-first input semantics with a project-owned typed intermediate
representation. JSON/YAML example inputs are allowed for early MVP models and
must pass the same typed validation path.

## Consequences

The generator owns semantic validation and traceability. ARXML coverage can grow
without changing generated runtime contracts.

## Evidence

- `requirements/autosar/release-profile.yaml`
- `tools/oa_cli/model.py`
- `model/examples/vehicle/ultrasonic_service.json`
