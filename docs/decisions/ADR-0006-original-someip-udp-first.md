<!-- SPDX-License-Identifier: MIT -->

# ADR-0006: Original SOME/IP UDP-First Implementation

## Status

Accepted

## Context

The platform needs SOME/IP behavior for service discovery and integration tests
without copying proprietary stack implementations.

## Decision

Implement an original SOME/IP subset in the project. UDP is the first transport
for the vertical slice; TCP support is a later extension after the UDP contract
is stable.

## Consequences

The message codec, request correlation, service discovery, and UDP endpoint are
owned by the project and tested directly.

## Evidence

- `bsw/communication/someip-protocol`
- `bsw/communication/someip-binding`
- `tests/unit/someip_protocol_test.cpp`
