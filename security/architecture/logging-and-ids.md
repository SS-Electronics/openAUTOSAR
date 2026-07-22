<!-- SPDX-License-Identifier: MIT -->

# Logging And IDSM

Security-relevant events are routed to IDSM where possible. Event records carry
source, category, severity, principal, resource, operation, detail, and timestamp
metadata.

Supportability outputs follow two rules:

- logs are bounded to recent excerpts;
- configuration and logs are redacted before entering debug bundles.

Authorization denials from diagnostics, service registry, firewall, time/network
firewall installation, and API gateway paths are expected to be auditable.
