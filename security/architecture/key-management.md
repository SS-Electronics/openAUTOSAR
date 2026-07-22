<!-- SPDX-License-Identifier: MIT -->

# Key Management

The MVP uses a reviewed crypto-provider boundary with key slots. Policies name
slots such as `slot://dev-signing`; key material is not stored in repository
source, generated evidence, or support bundles.

Required controls:

- non-exportable signing and MAC key slots;
- algorithm and usage pinned in policy;
- import rejection for empty or exportable key material;
- deterministic receipts for verification;
- release checklist review before adding production signing keys.
