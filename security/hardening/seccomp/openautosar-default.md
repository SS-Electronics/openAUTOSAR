<!-- SPDX-License-Identifier: MIT -->

# Default Seccomp Profile

The initial profile records syscall groups rather than shipping a production
filter. This keeps the policy auditable while the QEMU MVP matures.

Allowed group:

- `@system-service`

Before production use, the syscall list must be generated from target traces,
reviewed independently, and tied to release evidence.
