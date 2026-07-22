<!-- SPDX-License-Identifier: MIT -->

# Secure Boot

The QEMU MVP does not claim production secure boot. The reference architecture
keeps the secure-boot boundary explicit so a BSP profile can bind it later.

Current controls:

- package trust and provenance are verified before activation;
- boot attempts are bounded by Update Management;
- A/B slot policy records rollback intent;
- production secure boot remains a BSP and hardware integration requirement.
