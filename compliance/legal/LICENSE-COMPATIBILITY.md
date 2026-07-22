<!-- SPDX-License-Identifier: MIT -->

# License Compatibility

Status: preliminary engineering control.

Project-authored source files use MIT licensing where the file format permits a
machine-readable SPDX marker. Third-party components retain their own licenses.

## Project-Owned Material

- runtime source;
- tests;
- model examples;
- Python tooling;
- Yocto metadata authored for this project;
- project documentation and policies.

## Third-Party Material

AGL, Linux, Qt, RAUC, QEMU, Yocto, OpenSSL, compilers, build tools, and similar
dependencies are not relicensed by this project. Their notices and source-offer
obligations must be represented in SBOM and release evidence.

## Intake Rules

- Prefer permissive dependencies for new project code.
- Record package names, versions, licenses, and provenance.
- Do not import generated code, examples, or schemas with unclear redistribution
  rights.
- Do not embed credentials, signing keys, private certificates, or customer
  material.

## Release Gate

Before release, run the license scan and OEM export, verify SBOM generation, and
review the artifact inventory for unexpected third-party or confidential files.
