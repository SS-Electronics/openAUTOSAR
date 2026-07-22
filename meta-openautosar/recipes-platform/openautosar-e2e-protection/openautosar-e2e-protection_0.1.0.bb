# SPDX-License-Identifier: MIT

SUMMARY = "openAUTOSAR E2E protection policy"
DESCRIPTION = "Transport-independent E2E protection defaults for service event payloads."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://openautosar-e2e-protection-policy.yaml"

inherit openautosar-app

RDEPENDS:${PN} += "openautosar-runtime"

do_install() {
    install -d ${D}${sysconfdir}/openautosar
    install -m 0644 \
        ${WORKDIR}/openautosar-e2e-protection-policy.yaml \
        ${D}${sysconfdir}/openautosar/e2e-protection-policy.yaml
}

FILES:${PN} += "${sysconfdir}/openautosar/e2e-protection-policy.yaml"
