<!-- SPDX-License-Identifier: MIT -->

# ADR-0004: QEMU x86-64 Current Target

## Status

Accepted

## Context

The initial product needs a deterministic target that can run on developer and
CI hosts without requiring physical automotive hardware.

## Decision

Use QEMU x86-64 as the only current target for the MVP and reference topology.
Hardware and HIL support remain later productization work.

## Consequences

Target scripts, machine deployment metadata, and package profiles prioritize the
virtual target. Hardware-specific assumptions are kept out of core logic.

## Evidence

- `deployment/machine/qemux86-64-agl-unagi.yaml`
- `scripts/deploy-qemu.sh`
- `build/toolchains/qemu-x86_64.cmake`
