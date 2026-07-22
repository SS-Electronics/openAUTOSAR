# SPDX-License-Identifier: MIT

SUMMARY = "openAUTOSAR State Management policy"
DESCRIPTION = "Function Group state model and request policy for qemux86-64 AGL."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://openautosar-state-management-policy.yaml"

inherit openautosar-app

RDEPENDS:${PN} += "openautosar-runtime"

do_install() {
    install -d ${D}${sysconfdir}/openautosar
    install -m 0644 \
        ${WORKDIR}/openautosar-state-management-policy.yaml \
        ${D}${sysconfdir}/openautosar/state-management-policy.yaml
}

FILES:${PN} += "${sysconfdir}/openautosar/state-management-policy.yaml"
