<!-- SPDX-License-Identifier: MIT -->

# Quality Gate Policy

Quality evidence is controlled by three source artifacts:

- `requirements/quality/metrics.yaml`;
- `requirements/verification/fault-injection.yaml`;
- `requirements/performance/budgets.yaml`.

The release gate validates that every required quality metric and fault-injection
scenario is represented, and that each component budget records target,
cold-start, service-discovery, local-IPC, memory, CPU, and restart limits.

The QEMU values are engineering budgets for the virtual target. Final ECU values
must be derived from the vehicle use case and target hardware profile.
