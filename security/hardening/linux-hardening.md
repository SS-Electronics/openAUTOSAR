<!-- SPDX-License-Identifier: MIT -->

# Linux Hardening

The Linux profile runs platform services as non-root by default and applies
process-launcher limits through systemd-compatible policy.

Baseline controls:

- dedicated `openautosar` user and group;
- cgroup v2 memory and task budgets;
- file descriptor limits;
- no new privileges;
- private temporary directories;
- restricted device access;
- explicit capability allow list;
- restart budgets with escalation.
