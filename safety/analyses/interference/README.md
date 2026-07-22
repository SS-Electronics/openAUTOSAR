<!-- SPDX-License-Identifier: MIT -->

# Interference Analysis

Interference controls for mixed workloads:

- separate process identities;
- cgroup memory and task limits;
- local IPC endpoint labels;
- IAM-gated service operations;
- bounded queues and payload sizes;
- file namespace policy for persistency;
- no root execution by default.

The MVP does not claim freedom from interference for safety-related applications.
