#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

"${script_dir}/validate-ci-contract.sh"
"${script_dir}/validate-compliance.sh"
"${script_dir}/validate-architecture-work-products.sh"
"${script_dir}/validate-architecture-decisions.sh"
"${script_dir}/validate-program-governance.sh"
"${script_dir}/validate-implementation-plan.sh"
"${script_dir}/validate-tool-confidence.sh"
"${script_dir}/validate-api-abi.sh"
"${script_dir}/validate-quality-metrics.sh"
"${script_dir}/validate-classic-integration.sh"
"${script_dir}/validate-delivery-workflow.sh" --source-only
"${script_dir}/test.sh" --preset host-release
