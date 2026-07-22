# SPDX-License-Identifier: MIT

SUMMARY = "openAUTOSAR Remote Persistency policy"
DESCRIPTION = "Represented remote persistency guardrails without a production backend."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://openautosar-remote-persistency-policy.yaml"

inherit openautosar-app

RDEPENDS:${PN} += "openautosar-runtime openautosar-security"

do_install() {
    install -d ${D}${sysconfdir}/openautosar
    install -m 0644 \
        ${WORKDIR}/openautosar-remote-persistency-policy.yaml \
        ${D}${sysconfdir}/openautosar/remote-persistency-policy.yaml
}

FILES:${PN} += "${sysconfdir}/openautosar/remote-persistency-policy.yaml"
