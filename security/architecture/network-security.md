<!-- SPDX-License-Identifier: MIT -->

# Network Security

Network security is enforced through layered controls:

- Time and Network Management owns endpoint policy and clock-loss behavior.
- Firewall Management compiles deterministic nftables and eBPF plans.
- Service Registry operations are IAM gated.
- SOME/IP, DDS, and local IPC parsers reject malformed inputs.
- Diagnostics over virtual CAN requires tester authorization.
- API Gateway routes require authenticated clients and per-route limits.

The QEMU profile treats `tap-openautosar0` and `can0` as controlled virtual
interfaces, not as production vehicle network definitions.
