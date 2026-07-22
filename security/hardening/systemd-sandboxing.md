<!-- SPDX-License-Identifier: MIT -->

# Systemd Sandboxing

Systemd units and process-launcher policy provide the first sandboxing layer.

Required unit properties:

- `NoNewPrivileges=true`;
- `PrivateTmp=true`;
- bounded restart behavior;
- explicit environment file;
- service user set to `openautosar`;
- runtime directories owned by the service account.

Additional restrictions are profile-specific and must be validated before
production use.
