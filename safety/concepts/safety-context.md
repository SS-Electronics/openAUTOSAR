<!-- SPDX-License-Identifier: MIT -->

# Safety Context

The current platform provides safety mechanisms but is not itself qualified as a
safety element out of context. It can host QM applications in the MVP and expose
mechanisms that a future safety case can reuse.

This decision prevents accidental ISO 26262 claims while allowing the reference
implementation to build supervision, E2E checks, degraded modes, and resource
partitioning in a controlled way.
