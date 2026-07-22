#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

cmake --version >/dev/null
c++ --version >/dev/null
python3 --version >/dev/null

mkdir -p "${repo_root}/out"
PYTHONPATH="${repo_root}/tools" python3 -m compileall -q "${repo_root}/tools/oa_cli"
PYTHONPATH="${repo_root}/tools" python3 -m oa_cli host-info --output "${repo_root}/out/host-info.json"

echo "bootstrap complete: ${repo_root}/out/host-info.json"
