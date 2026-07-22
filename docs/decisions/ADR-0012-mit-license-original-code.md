<!-- SPDX-License-Identifier: MIT -->

# ADR-0012: MIT License for Original Code

## Status

Accepted

## Context

The project needs a permissive license for original project-authored code while
keeping third-party and AUTOSAR IP boundaries explicit.

## Decision

License original project-authored code under MIT. Legal work products track
third-party notices, branding boundaries, and AUTOSAR IP restrictions.

## Consequences

Source files carry SPDX metadata, release artifacts include legal reports, and
the project does not claim ownership of AUTOSAR specifications.

## Evidence

- `LICENSE`
- `compliance/legal`
- `ci/checks/license-scan.sh`
