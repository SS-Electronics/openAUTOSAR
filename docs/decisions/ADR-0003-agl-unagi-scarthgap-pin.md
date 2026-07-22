<!-- SPDX-License-Identifier: MIT -->

# ADR-0003: AGL Unagi and Yocto Scarthgap Pin

## Status

Accepted

## Context

The reference image needs a reproducible Linux baseline for CI, package
metadata, and QEMU validation.

## Decision

Pin the reference integration to AGL Ultimate Unagi 21.0.2 and Yocto Scarthgap
5.0.18 for the current platform baseline.

## Consequences

Layer metadata, image recipes, and validation scripts target this known baseline.
Moving to a later AGL or Yocto release requires a new ADR and migration evidence.

## Evidence

- `meta-openautosar/conf/layer.conf`
- `meta-openautosar/conf/distro/include/openautosar-agl-pin.inc`
- `scripts/validate-yocto-layer.sh`
