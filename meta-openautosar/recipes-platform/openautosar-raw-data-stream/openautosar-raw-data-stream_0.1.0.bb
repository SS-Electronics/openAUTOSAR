# SPDX-License-Identifier: MIT

SUMMARY = "openAUTOSAR raw data stream policy"
DESCRIPTION = "Bounded raw data stream framing defaults for Adaptive communication."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://openautosar-raw-data-stream-policy.yaml"

inherit openautosar-app

RDEPENDS:${PN} += "openautosar-runtime"

do_install() {
    install -d ${D}${sysconfdir}/openautosar
    install -m 0644 \
        ${WORKDIR}/openautosar-raw-data-stream-policy.yaml \
        ${D}${sysconfdir}/openautosar/raw-data-stream-policy.yaml
}

FILES:${PN} += "${sysconfdir}/openautosar/raw-data-stream-policy.yaml"
