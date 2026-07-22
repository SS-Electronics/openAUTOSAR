<!-- SPDX-License-Identifier: MIT -->

# API And ABI Policy

The MVP exports project APIs under `openautosar::*`. Project-specific extensions
must use `openautosar::extension::*`. Non-standard APIs must not be placed in
`ara::*`.

## Versioning

The CMake project version is the release-pinned package version. Public package
metadata is generated through `OpenAutosarConfigVersion.cmake` with same-major
compatibility. This is the project semantic versioning source of truth.

## Visibility

Targets are built with hidden C++ visibility by default and hidden inline
visibility enabled. Static libraries are used for the MVP package; these
settings remain required so shared-library profiles can reuse the same policy.
This is the baseline symbol visibility control for public API builds.

## ABI Baseline

Each release records a public header baseline under
`out/evidence/api-abi/public-header-baseline.json`. A later release can compare
that content-addressed baseline against the new package.
This file is the initial ABI baseline record for the package.

## Public API Rules

- public headers live under `include/openautosar`;
- headers carry SPDX metadata;
- public APIs use documented project namespaces;
- stable error-domain types stay under `openautosar::core`;
- generated APIs are checked by model tests and traceability output;
- deprecations require release-note and compatibility-review evidence;
- internal implementation types must not leak through public headers.
