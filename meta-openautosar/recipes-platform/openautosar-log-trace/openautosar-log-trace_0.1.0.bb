# SPDX-License-Identifier: MIT

SUMMARY = "openAUTOSAR Log and Trace policy"
DESCRIPTION = "Rate, buffer, backend, and record-field defaults for Adaptive logging."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://openautosar-log-trace-policy.yaml"

inherit openautosar-app

RDEPENDS:${PN} += "openautosar-runtime"

do_install() {
    install -d ${D}${sysconfdir}/openautosar
    install -m 0644 \
        ${WORKDIR}/openautosar-log-trace-policy.yaml \
        ${D}${sysconfdir}/openautosar/log-trace-policy.yaml

    install -d ${D}${localstatedir}/log/openautosar
}

FILES:${PN} += "${sysconfdir}/openautosar/log-trace-policy.yaml"
