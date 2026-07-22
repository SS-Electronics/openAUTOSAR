<!-- SPDX-License-Identifier: MIT -->

# Trust Boundaries

The initial platform uses these trust boundaries:

- Developer workstation to CI agent: source changes enter controlled validation.
- Model import to generator: untrusted ARXML or JSON is normalized and validated.
- Generator to runtime package: generated files are immutable release artifacts.
- Process launcher to applications: identities, cgroups, namespaces, and labels apply.
- Local IPC clients to services: peer identity and endpoint labels are verified.
- Network peers to SOME/IP and DDS bindings: payloads and discovery traffic are bounded.
- Diagnostic tester to vehicle services: security access and IAM policy gate actions.
- Update backend to UCM: packages require provenance, SBOM, and signature policy.
- Runtime to evidence export: logs and debug bundles are bounded and redacted.
- Release signer to repository: signing keys never enter pull-request jobs.
