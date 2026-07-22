<!-- SPDX-License-Identifier: MIT -->

# ADR-0009: UDS over SocketCAN vcan Diagnostics

## Status

Accepted

## Context

Diagnostics need an early transport path that can be tested without vehicle
hardware or a commercial Classic stack.

## Decision

Use UDS over ISO-TP-style single-frame SocketCAN `vcan` as the first diagnostic
transport for the SIL reference platform.

## Consequences

The diagnostic manager, Classic gateway, and virtual vehicle tests can exercise
diagnostic requests and DTC memory through a deterministic virtual CAN path.

## Evidence

- `bsw/runtime/diagnostic-management`
- `integration/virtual-vehicle/gateway-peer`
- `integration/virtual-vehicle/classic-gateway/gateway-config.json`
- `tests/unit/gateway_diagnostic_test.cpp`
