<!-- SPDX-License-Identifier: MIT -->

# Public API Index

## openautosar/core

Purpose: common result, error, future/promise, and instance specifier vocabulary.
Lifecycle: initialized by value and owned by the caller.
Threading: immutable values are thread-safe after construction.
Ownership: APIs avoid transferring raw ownership.
Errors: failures are represented by typed error values.
Timing: operations are expected to be local and bounded.
Example: construct an instance specifier, return `Result<T>`, and satisfy a
`Future<T>` through a `Promise<T>`.
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

Purpose: service records, service registry, generated service-facing types,
proxy/skeleton facades, method futures, method call/result queues, no-return
methods, generated method error domains, payload-less triggers, and field
getter/setter/notifier state.
Lifecycle: records are registered during service startup and removed during shutdown.
Threading: registry snapshots are value copies.
Ownership: service metadata is copied into registry-owned storage.
Errors: duplicate or malformed service records return errors; structured method
application errors carry generated error domains and numeric codes.
Timing: discovery and snapshot paths are bounded by registry size.
Example: generate `UltrasonicDistanceServiceProxy` and `Skeleton` wrappers,
publish events, complete a correlated method call, wait through a method future,
fire a trigger, or dispatch a fire-and-forget method through the registry; get,
set, and subscribe to generated service fields.
Compatibility status: experimental service semantic layer.

## openautosar/local_ipc

Purpose: same-machine local IPC binding frames, endpoint leases, peer admission,
queue/backpressure policy, events, methods, fields, triggers, and deterministic
fault-injection hooks.
Lifecycle: endpoints are offered, leased, stopped, restarted, and cleaned up by
the binding owner.
Threading: endpoint use is externally serialized.
Ownership: queued frames, method responses, and field values are value-owned by
the binding until polled.
Errors: stale endpoints, unauthorized peers, malformed mappings, duplicate
method correlations, and queue backpressure are reported explicitly.
Timing: endpoint leases and poll timeouts use caller-provided monotonic time.
Example: offer a local endpoint, subscribe a peer, exchange event frames, route a
correlated method reply with generated error metadata, update a field, accept a
field-set request, and fire a payload-less trigger.
Compatibility status: deterministic in-process model for the local IPC binding.

## openautosar/someip

Purpose: original SOME/IP message, service-discovery, method request/response
and request-no-return mapping, field getter/setter/notifier mapping, UDP endpoint,
and TCP stream primitives.
Lifecycle: codecs are stateless and endpoints/connections are explicitly opened and closed.
Threading: endpoints are externally serialized by their owner.
Ownership: serialized buffers are caller-owned values.
Errors: malformed packets return parse errors.
Timing: request correlation uses bounded expiration checks.
Example: encode a method request, correlate a response, encode a no-return method,
encode field getter/setter/notifier traffic, and decode messages from a UDP
datagram or TCP stream.
Compatibility status: UDP-first bootstrap subset with bounded TCP stream transport.

## openautosar/dds

Purpose: original RTPS message, SPDP participant discovery, SEDP endpoint discovery,
static QoS profile matching, deadline/liveliness/resource-limit checks, best-effort
writer/reader path, bounded reliable repair, transient-local history replay, and UDP
endpoint primitives.
Method request/reply and no-response topic mapping carries service identity,
method name, payload, correlation, and application-error status in bounded RTPS
DATA envelopes. Structured method application errors preserve generated error
domain and numeric code metadata. Field notifier topic mapping carries service
identity, field name, field sequence, and value payload in bounded RTPS DATA
envelopes.
Lifecycle: codecs are stateless; discovery caches and transport endpoints are owned by the caller.
Threading: endpoint use is externally serialized.
Ownership: datagrams and submessages are value-owned.
Errors: malformed RTPS fields return parse errors.
Timing: discovery and liveliness behavior is bounded by configured policies.
Example: map reliability/durability/history/deadline/liveliness QoS, match peers, exchange
DATA, heartbeat writer history, ACKNACK missing samples, and replay cached samples to late
transient-local readers; encode method request/reply and no-response calls over
configured RTPS topics; encode field notifier updates over configured RTPS topics.
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
