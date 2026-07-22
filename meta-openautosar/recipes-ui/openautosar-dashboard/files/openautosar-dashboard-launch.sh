#!/usr/bin/env sh
# SPDX-License-Identifier: MIT

set -eu

snapshot="${OA_DASHBOARD_SNAPSHOT:-/var/lib/openautosar/dashboard-snapshot.json}"
qml_dir="${OA_DASHBOARD_QML_DIR:-/usr/share/openautosar/dashboard/qml}"
qml_main="${OA_DASHBOARD_QML_MAIN:-${qml_dir}/OpenAutosarDashboard.qml}"

/usr/bin/oa-dashboard-snapshot \
  --samples "${OA_DASHBOARD_SAMPLES:-6}" \
  --fault "${OA_DASHBOARD_FAULT:-crc_error}" \
  --fault-at "${OA_DASHBOARD_FAULT_AT:-3}" \
  --output "${snapshot}"

exec /usr/bin/qml -I "${qml_dir}" "${qml_main}"
