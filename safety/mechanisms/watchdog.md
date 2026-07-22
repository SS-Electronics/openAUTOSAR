<!-- SPDX-License-Identifier: MIT -->

# Watchdog

The MVP models watchdog behavior through PHM supervision and restart budgets.
Production watchdog integration remains a BSP-specific task.

Required behavior:

- missed alive indication is reported;
- restart is bounded;
- repeated failure escalates to degraded state;
- evidence records the triggering condition.
