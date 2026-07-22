<!-- SPDX-License-Identifier: MIT -->

# Degraded Modes

The state model includes `Degraded` as an explicit function-group state. Runtime
components should enter degraded behavior when supervision, network, update, or
clock policies report a condition that prevents normal operation.

Degraded mode is auditable and must keep diagnostic and supportability paths
available when policy permits them.
