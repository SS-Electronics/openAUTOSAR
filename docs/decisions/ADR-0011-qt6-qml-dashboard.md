<!-- SPDX-License-Identifier: MIT -->

# ADR-0011: Qt 6 QML Engineering Dashboard

## Status

Accepted

## Context

The MVP needs a visual engineering dashboard for SIL state, diagnostics, and
health evidence without becoming an HMI product.

## Decision

Use Qt 6 and QML for the engineering dashboard. The dashboard remains a
diagnostic and supportability view, not the authoritative runtime controller.

## Consequences

Dashboard artifacts can be packaged and inspected while runtime logic remains in
the platform services and generated evidence.

## Evidence

- `apps/dashboard/openautosar-dashboard`
- `meta-openautosar/recipes-ui/openautosar-dashboard`
- `tests/unit/dashboard_snapshot_test.cpp`
