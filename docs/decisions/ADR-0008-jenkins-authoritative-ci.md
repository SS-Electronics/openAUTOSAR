<!-- SPDX-License-Identifier: MIT -->

# ADR-0008: Jenkins as Authoritative CI

## Status

Accepted

## Context

The project needs a CI contract that can be run on controlled infrastructure and
can collect build, test, package, and evidence artifacts.

## Decision

Use Jenkins as the authoritative CI entry point through a shared pipeline
library. The repository validates the expected stages and commands.

## Consequences

Other CI systems may mirror the checks, but Jenkins defines the required release
gate shape for this baseline.

## Evidence

- `Jenkinsfile`
- `ci/jenkins/shared/vars/openAutosarPipeline.groovy`
- `scripts/validate-ci-contract.sh`
