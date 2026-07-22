<!-- SPDX-License-Identifier: MIT -->

# Identities

Runtime authorization is based on explicit principals rather than process names
alone. A principal records:

- stable identifier;
- security label;
- roles;
- optional process or service provenance.

The IAM policy engine evaluates operations against resource kind, resource ID,
roles, labels, and explicit allow/deny decisions. Missing principals are denied
for protected communication, diagnostic, gateway, and firewall operations.
