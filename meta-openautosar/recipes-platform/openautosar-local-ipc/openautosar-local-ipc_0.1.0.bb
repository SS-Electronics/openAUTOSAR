# SPDX-License-Identifier: MIT

SUMMARY = "openAUTOSAR local IPC binding policy"
DESCRIPTION = "Local IPC communication defaults for same-machine ara::com services."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://openautosar-local-ipc-policy.yaml"

inherit openautosar-app

RDEPENDS:${PN} += "openautosar-runtime"

do_install() {
    install -d ${D}${sysconfdir}/openautosar
    install -m 0644 \
        ${WORKDIR}/openautosar-local-ipc-policy.yaml \
        ${D}${sysconfdir}/openautosar/local-ipc-policy.yaml

    install -d ${D}${runtimedir}/openautosar/ipc
}

FILES:${PN} += "${sysconfdir}/openautosar/local-ipc-policy.yaml ${runtimedir}/openautosar/ipc"
