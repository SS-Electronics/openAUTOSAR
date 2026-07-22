<!-- SPDX-License-Identifier: MIT -->

# Package Trust

Software Cluster packages must include manifest data, provenance, SBOM evidence,
and signature policy input before UCM accepts activation.

Trust decisions are based on:

- profile and target compatibility;
- trusted signer IDs;
- package digest and manifest validity;
- SBOM presence;
- rollback availability;
- audit trail for install, activate, and rollback decisions.
