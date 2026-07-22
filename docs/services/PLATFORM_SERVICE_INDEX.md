<!-- SPDX-License-Identifier: MIT -->

# Platform Service Index

## Execution Management

Context diagram: process launcher, bootstrapper, and systemd unit boundary.
Sequence diagram: bootstrap validates machine policy, starts processes, and reports state.
State machine: process states move through configured lifecycle states only.
Trust boundary: process identities and service users are validated before launch.
Deployment model: packaged binary plus systemd policy under the QEMU machine profile.
Failure table: duplicate process records, failed launch, and restart escalation are tracked.
Verification strategy: unit tests and CI package inspection.

## State Management

Context diagram: Function Groups coordinate process and machine state.
Sequence diagram: clients request state and receive ordered transition actions.
State machine: MachineFG starts in Off and transitions through Startup to DrivingReady.
Trust boundary: only configured policy can define states and transitions.
Deployment model: state policy is staged with the platform package.
Failure table: invalid transition and unknown Function Group errors are explicit.
Verification strategy: unit tests plus dashboard and bootstrap smoke paths.

## Communication Management

Context diagram: ara com registry, SOME/IP binding, DDS binding, and local IPC boundary.
Sequence diagram: model generation creates service types, then services register and discover.
State machine: service records move through registered, available, and removed lifecycle.
Trust boundary: generated model inputs are validated before runtime use.
Deployment model: libraries and generated model artifacts are staged in the SDK package.
Failure table: duplicate service, malformed model, and stale endpoint cases are tested.
Verification strategy: unit tests, golden vectors, and network-lab smoke evidence.

## Diagnostics Management

Context diagram: diagnostic manager, Classic gateway, UDS, and SocketCAN vcan.
Sequence diagram: fault event records DTC state and gateway returns diagnostic response.
State machine: DTC records move through inactive, active, reported, and cleared policy states.
Trust boundary: diagnostic service subset and DTC ownership remain controlled by policy.
Deployment model: diagnostics library, gateway peer, and vcan scenario runner.
Failure table: unsupported DID, unknown routine, and stale gateway sample paths are covered.
Verification strategy: unit tests, Classic integration validator, and network-lab evidence.

## Update Management

Context diagram: UCM orchestration above a replaceable RAUC A/B backend.
Sequence diagram: package is staged, verified, activated, monitored, and rolled back on failure.
State machine: update transactions progress through staged, activated, confirmed, or failed.
Trust boundary: package signatures and slot ownership are validated by policy.
Deployment model: vehicle UCM policy and package provenance are staged with the image.
Failure table: inactive-slot conflict, health failure, and rollback requirements are tracked.
Verification strategy: unit tests, package provenance, and OEM export evidence.

## Platform Health Management

Context diagram: supervised entities, health reports, and recovery actions.
Sequence diagram: entities register, report checkpoints, and receive recovery decisions.
State machine: supervised entities transition through healthy, degraded, and failed states.
Trust boundary: only configured supervision policies can drive recovery actions.
Deployment model: PHM library and quality/fault-injection evidence.
Failure table: missed checkpoint, deadline breach, and restart escalation are modeled.
Verification strategy: unit tests and quality-metrics validation.

## Security Services

Context diagram: crypto provider, identity access manager, firewall, and IDSM.
Sequence diagram: identity is authorized, policy is evaluated, and security events are recorded.
State machine: rules and identities are loaded, evaluated, and reported.
Trust boundary: security policy, key slots, and event export are controlled by deployment config.
Deployment model: security recipes and policies are staged into the package.
Failure table: denied identity, blocked flow, unsupported algorithm, and IDSM threshold breach.
Verification strategy: unit tests, compliance validation, and OEM export inventory.

## Time And Network Management

Context diagram: virtual clock domain, network interface policy, and service endpoints.
Sequence diagram: clock status and interface readiness gate endpoint traffic.
State machine: interfaces move between down, degraded, and ready policy states.
Trust boundary: network and timing policy are loaded from package-controlled config.
Deployment model: time-network policy is installed with the QEMU machine profile.
Failure table: loss of sync, interface down, and endpoint policy mismatch.
Verification strategy: unit tests and debug bundle health evidence.
