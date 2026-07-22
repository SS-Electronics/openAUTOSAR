<!-- SPDX-License-Identifier: MIT -->

# ADR-0002: C++20 and R22-11-Oriented ABI Policy

## Status

Accepted

## Context

Adaptive Platform APIs need a modern C++ baseline while preserving a controlled
public surface for generated bindings and SDK consumers.

## Decision

Use C++20 for project-authored runtime and host-facing C++ code. Treat the public
SDK as R22-11-oriented and validate public headers against the API/ABI policy.

## Consequences

The build can use C++20 library and language features, but exported APIs need
stable names, namespaces, includes, and compatibility review.

## Evidence

- `build/cmake/OpenAutosarCompilerOptions.cmake`
- `docs/api/API_ABI_POLICY.md`
- `requirements/api/public-api-baseline.yaml`
- `scripts/validate-api-abi.sh`
