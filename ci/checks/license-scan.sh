#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
missing=0

while IFS= read -r path; do
  if ! head -n 5 "${repo_root}/${path}" | grep -q "SPDX-License-Identifier: MIT"; then
    echo "missing SPDX-License-Identifier: MIT: ${path}" >&2
    missing=1
  fi
done < <(
  cd "${repo_root}"
  find . \
    -path './.git' -prune -o \
    -path './out' -prune -o \
    -type f \( \
      -name '*.cpp' -o \
      -name '*.h' -o \
      -name '*.py' -o \
      -name '*.sh' -o \
      -name '*.groovy' -o \
      -name '*.bb' -o \
      -name '*.bbclass' -o \
      -name '*.inc' -o \
      -name '*.wks' -o \
      -name 'CMakeLists.txt' -o \
      -name '*.cmake' -o \
      -name 'Jenkinsfile' -o \
      -name '.clang-format' \
    \) \
    -print | sed 's#^\./##' | sort
)

exit "${missing}"
