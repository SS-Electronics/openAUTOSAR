# openAUTOSAR

`openAUTOSAR` is the working repository name for an original Adaptive Platform-oriented automotive runtime. The current implementation baseline is a QEMU x86-64 host build that uses C++20, CMake, Python tooling, and MIT-licensed project-authored code.

This repository does not include AUTOSAR specifications, schemas, downloaded PDFs, or proprietary OEM inputs. The initial build focus is a clean host bootstrap for the Adaptive/QEMU direction, not the previous Cortex-M/Classic firmware tree.

## Quick Start

```bash
./scripts/bootstrap.sh
./scripts/configure.sh --target qemu-x86_64 --profile dev
./scripts/generate.sh --model model/examples/vehicle
./scripts/build.sh
./scripts/test.sh
./scripts/package.sh --profile generic-oem
```

The default build artifacts are written to `out/` and are intentionally ignored by Git.

## Current Scope

- C++20 runtime component skeletons for core types, logging, execution management, and platform bootstrap.
- Python host tooling entry point for model validation, deterministic generation metadata, and provenance capture.
- CMake presets for host and QEMU x86-64 development builds.
- Jenkins smoke pipeline for bootstrap, configure, generate, build, test, license scan, and package stages.

AGL image boot, QEMU deployment, SOME/IP, DDS, UCM/RAUC, diagnostics, and virtual vehicle integration are planned follow-on workstreams.
