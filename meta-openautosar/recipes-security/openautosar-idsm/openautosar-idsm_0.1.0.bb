# SPDX-License-Identifier: MIT

SUMMARY = "openAUTOSAR intrusion detection policy"
DESCRIPTION = "Bounded IDSM normalization, throttling, privacy, and forwarding policy."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://openautosar-idsm-policy.yaml"

inherit openautosar-app

RDEPENDS:${PN} += "openautosar-runtime openautosar-security"

do_install() {
    install -d ${D}${sysconfdir}/openautosar
    install -m 0644 \
        ${WORKDIR}/openautosar-idsm-policy.yaml \
        ${D}${sysconfdir}/openautosar/idsm-policy.yaml
}

FILES:${PN} += "${sysconfdir}/openautosar/idsm-policy.yaml"
