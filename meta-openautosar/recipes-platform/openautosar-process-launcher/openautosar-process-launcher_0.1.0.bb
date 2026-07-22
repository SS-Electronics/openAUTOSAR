# SPDX-License-Identifier: MIT

SUMMARY = "openAUTOSAR process launcher policy"
DESCRIPTION = "Execution Management process identity, systemd scope, and cgroup policy."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://openautosar-process-launcher-policy.yaml"

inherit openautosar-app

RDEPENDS:${PN} += "openautosar-runtime systemd"

do_install() {
    install -d ${D}${sysconfdir}/openautosar
    install -m 0644 \
        ${WORKDIR}/openautosar-process-launcher-policy.yaml \
        ${D}${sysconfdir}/openautosar/process-launcher-policy.yaml
}

FILES:${PN} += "${sysconfdir}/openautosar/process-launcher-policy.yaml"
