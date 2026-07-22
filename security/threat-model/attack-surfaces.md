<!-- SPDX-License-Identifier: MIT -->

# Attack Surfaces

Minimum surfaces tracked for the reference implementation:

- ARXML and model import;
- generated code and templates;
- execution, service, and machine manifests;
- local IPC;
- SOME/IP payloads and service discovery;
- DDS payloads and discovery;
- diagnostics over virtual CAN;
- update transfer and metadata import;
- package activation and rollback;
- persistency storage and remote persistency keys;
- log collection and debug bundles;
- external update/backend interfaces;
- development CI and artifact export;
- release signing system.

Each surface is represented in `threats.yaml` with preventive and detective
controls that map back to platform components or release evidence.
