<!-- SPDX-License-Identifier: MIT -->

# ADR-0010: UCM over Replaceable RAUC A/B Backend

## Status

Accepted

## Context

The update architecture needs A/B rollback behavior but must keep backend
implementation replaceable for future target choices.

## Decision

Model UCM over a replaceable RAUC A/B backend. The MVP uses a simulator and
policy evidence rather than claiming production update infrastructure.

## Consequences

Update sequencing, activation, commit, and rollback are testable now. Hardware
backend integration remains behind the update backend interface.

## Evidence

- `bsw/runtime/update-management`
- `bsw/runtime/vehicle-update-management`
- `meta-openautosar/wic/openautosar-reference-ab.wks`
- `tests/unit/update_management_test.cpp`
