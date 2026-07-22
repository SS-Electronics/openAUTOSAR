# SPDX-License-Identifier: MIT

SUMMARY = "openAUTOSAR production-oriented image placeholder"
LICENSE = "MIT"

inherit core-image

IMAGE_FEATURES:remove = "debug-tweaks"

IMAGE_INSTALL:append = " \
    openautosar-runtime \
    openautosar-log-trace \
    openautosar-local-ipc \
    openautosar-process-launcher \
    openautosar-state-management \
    openautosar-registry \
    openautosar-vehicle-ucm \
    openautosar-e2e-protection \
    openautosar-raw-data-stream \
    openautosar-automotive-api-gateway \
    openautosar-remote-persistency \
    openautosar-safe-hardware-acceleration \
    openautosar-crypto \
    openautosar-security \
    openautosar-firewall \
    openautosar-idsm \
    openautosar-time-network \
"
