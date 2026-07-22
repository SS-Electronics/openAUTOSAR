<!-- SPDX-License-Identifier: MIT -->

# Public API Index

## openautosar/core

Purpose: common result, error, and instance specifier vocabulary.
Lifecycle: initialized by value and owned by the caller.
Threading: immutable values are thread-safe after construction.
Ownership: APIs avoid transferring raw ownership.
Errors: failures are represented by typed error values.
Timing: operations are expected to be local and bounded.
Example: construct an instance specifier and return `Result<T>`.
Compatibility status: bootstrap-stable.

## openautosar/runtime

Purpose: execution, state, health, persistency, update, registry, and diagnostics services.
Lifecycle: managers are explicitly constructed and owned by the bootstrapper.
Threading: callers retain ownership of synchronization around shared managers.
Ownership: registered entities are copied into manager-owned records.
Errors: invalid state transitions and duplicate registrations return errors.
Timing: deadline-sensitive APIs use monotonic-time semantics.
Example: request a Function Group state and inspect returned actions.
Compatibility status: experimental R22-11-aligned surface.

## openautosar/com

Purpose: service records, service registry, and generated service-facing types.
Lifecycle: records are registered during service startup and removed during shutdown.
Threading: registry snapshots are value copies.
Ownership: service metadata is copied into registry-owned storage.
Errors: duplicate or malformed service records return errors.
Timing: discovery and snapshot paths are bounded by registry size.
Example: register `UltrasonicDistanceService` and query a snapshot.
Compatibility status: experimental service semantic layer.

## openautosar/someip

Purpose: original SOME/IP message, service-discovery, UDP endpoint, and TCP stream primitives.
Lifecycle: codecs are stateless and endpoints/connections are explicitly opened and closed.
Threading: endpoints are externally serialized by their owner.
Ownership: serialized buffers are caller-owned values.
Errors: malformed packets return parse errors.
Timing: request correlation uses bounded expiration checks.
Example: encode a request message and decode it from a UDP datagram or TCP stream.
Compatibility status: UDP-first bootstrap subset with bounded TCP stream transport.

## openautosar/dds

Purpose: original RTPS message, SPDP participant discovery, SEDP endpoint discovery,
static QoS profile matching, deadline/liveliness/resource-limit checks, best-effort
writer/reader path, bounded reliable repair, transient-local history replay, and UDP
endpoint primitives.
Lifecycle: codecs are stateless; discovery caches and transport endpoints are owned by the caller.
Threading: endpoint use is externally serialized.
Ownership: datagrams and submessages are value-owned.
Errors: malformed RTPS fields return parse errors.
Timing: discovery and liveliness behavior is bounded by configured policies.
Example: map reliability/durability/history/deadline/liveliness QoS, match peers, exchange
DATA, heartbeat writer history, ACKNACK missing samples, and replay cached samples to late
transient-local readers.
Compatibility status: bootstrap subset with deterministic discovery, static QoS profiles,
best-effort DATA, repair, and transient-local history replay.

## openautosar/security

Purpose: crypto provider, identity, firewall, and IDSM management APIs.
Lifecycle: policies are loaded before service activation.
Threading: manager mutation is caller-serialized.
Ownership: rules, keys, identities, and events are copied into manager-owned storage.
Errors: unsupported algorithms, duplicate rules, and invalid identities return errors.
Timing: checks are bounded by configured policy sizes.
Example: authorize a service identity and record an IDSM event.
Compatibility status: security bootstrap surface.

## openautosar/safety

Purpose: safe hardware acceleration contracts and degraded-mode decision helpers.
Lifecycle: policies are configured during machine startup.
Threading: decision records are value-owned.
Ownership: caller owns input samples and manager owns copied policy data.
Errors: invalid policy or unsupported capability returns an error.
Timing: decisions are local and bounded.
Example: select a safe fallback when acceleration is unavailable.
Compatibility status: provisional safety surface.

## openautosar/virtual_vehicle

Purpose: ultrasonic, Classic PDU, SocketCAN, diagnostics, and gateway simulation APIs.
Lifecycle: models and endpoints are explicitly constructed by the scenario runner.
Threading: endpoint ownership is explicit and not detached.
Ownership: frames, samples, and diagnostics are value-owned.
Errors: invalid signals, stale samples, and transport failures return errors.
Timing: sample age, alive counters, and timeout checks use configured bounds.
Example: encode a Classic ultrasonic PDU and map it to a service sample.
Compatibility status: SIL reference surface.

## openautosar/dashboard

Purpose: engineering dashboard snapshot generation and QML-facing status model.
Lifecycle: snapshots are generated by a short-lived command or service.
Threading: snapshot generation reads package and evidence files synchronously.
Ownership: JSON snapshots are file artifacts owned by the dashboard service.
Errors: missing package, evidence, or policy inputs produce degraded status fields.
Timing: snapshot generation is bounded by local file reads.
Example: generate a dashboard snapshot for QEMU package evidence.
Compatibility status: engineering tool surface.
