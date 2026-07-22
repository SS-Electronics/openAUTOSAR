#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
preset="qemu-x86_64-dev"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --preset)
      preset="$2"
      shift 2
      ;;
    *)
      echo "unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

if [[ ! -f "${repo_root}/out/build/${preset}/CMakeCache.txt" ]]; then
  cmake --preset "${preset}" -S "${repo_root}"
fi

cmake --build --preset "${preset}"
