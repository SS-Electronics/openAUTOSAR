<!-- SPDX-License-Identifier: MIT -->

# Security Assets

This inventory defines assets for the QEMU-first Adaptive platform profile.

| Asset | Security property | Owner |
| --- | --- | --- |
| Vehicle model inputs | Integrity, provenance | Model toolchain |
| Generated manifests and code | Integrity, traceability | Generator pipeline |
| Service registry state | Integrity, availability | Communication Management |
| SOME/IP and DDS traffic | Integrity, freshness, availability | Communication bindings |
| Local IPC endpoints | Integrity, peer identity | Local IPC binding |
| Diagnostics access | Authorization, auditability | Diagnostic Management |
| Update packages | Authenticity, rollback safety | Update Management |
| Persisted data | Integrity, namespace isolation | Persistency |
| Logs and IDSM reports | Integrity, redaction | Log and Trace, IDSM |
| CI and release evidence | Reproducibility, provenance | CI/CD pipeline |
| Signing key slots | Confidentiality, authorized use | Crypto provider |
