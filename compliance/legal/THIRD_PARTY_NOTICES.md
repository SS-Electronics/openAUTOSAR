<!-- SPDX-License-Identifier: MIT -->

# Third-Party Notices

Status: preliminary engineering control.

The project currently stages release evidence and SBOM data through the OEM
export tooling. This file records notice handling rules until a complete notice
bundle is generated from the pinned image and package manifests.

## Notice Sources

- operating system image package manifests;
- Yocto layer metadata;
- build tool and compiler metadata;
- runtime dependency manifests;
- generated SBOM files;
- source package metadata.

## Required Review

Each release must confirm that third-party notices are available for:

- AGL and Yocto components;
- Linux and system libraries;
- Qt dashboard dependencies;
- RAUC or update tooling when included;
- OpenSSL or other cryptographic libraries;
- QEMU and host-side tooling used in deliverables.

## Current Limitation

The current host package is a QEMU-first engineering baseline. Image-level
third-party notice completeness depends on the pinned AGL/Yocto build output.
