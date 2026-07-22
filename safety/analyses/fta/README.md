<!-- SPDX-License-Identifier: MIT -->

# Fault Tree Analysis

Top event: platform presents unavailable or unsafe service state.

Contributing causes:

- service process fails to start;
- state transition is requested without authority;
- network interface is unavailable;
- generated manifest does not match runtime service;
- persisted configuration is corrupt;
- update activation interrupts required service.

Controls are allocated to Execution Management, State Management, PHM,
Communication Management, Persistency, and UCM.
