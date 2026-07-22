<!-- SPDX-License-Identifier: MIT -->

# ADR-0013: Ultrasonic Fail-Silent and Degraded Policy

## Status

Accepted

## Context

The virtual ultrasonic path must handle malformed, stale, or implausible Classic
signals without publishing unsafe distance data.

## Decision

Use a fail-silent policy for invalid ultrasonic samples and request degraded
state after repeated faults. Validated samples may publish distance events.

## Consequences

Fault injection, PHM, diagnostics, and dashboard evidence all observe the same
degraded behavior.

## Evidence

- `integration/virtual-vehicle/ultrasonic-sensor-model`
- `integration/virtual-vehicle/gateway-peer`
- `requirements/verification/fault-injection.yaml`
- `tests/unit/virtual_vehicle_test.cpp`
