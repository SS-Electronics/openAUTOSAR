<!-- SPDX-License-Identifier: MIT -->

# FMEA

Initial failure modes:

| Item | Failure mode | Platform response |
| --- | --- | --- |
| Process | Crash loop | Restart budget exhaustion and PHM report |
| Service registry | Stale provider | Lease expiry and rediscovery |
| Communication | Corrupt event | E2E rejection |
| Persistency | Corrupt value | Error result and recovery path |
| Update | Failed activation | Coordinated rollback |
| Clock | Loss of sync | Time/network degraded behavior |
