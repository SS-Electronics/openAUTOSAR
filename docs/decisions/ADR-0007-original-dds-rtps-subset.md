<!-- SPDX-License-Identifier: MIT -->

# ADR-0007: Original DDS/RTPS Subset

## Status

Accepted

## Context

The Adaptive communication roadmap needs a DDS/RTPS binding path while avoiding
dependency on a production DDS vendor stack for the reference MVP.

## Decision

Implement a bounded, original DDS/RTPS subset for the current reference use case.
The subset is explicit about unsupported features and is validated by unit tests.

## Consequences

The implementation remains inspectable and suitable for SIL evidence. Broader
DDS interoperability requires separate compatibility work and ADR updates.

## Evidence

- `bsw/communication/dds-rtps`
- `bsw/communication/dds-binding`
- `tests/unit/dds_rtps_test.cpp`
- `tests/unit/dds_binding_test.cpp`
