# SPDX-License-Identifier: MIT

SUMMARY = "openAUTOSAR time and network management policy"
DESCRIPTION = "Clock-domain, interface, endpoint, and firewall policy for qemux86-64 AGL."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://openautosar-time-network-policy.yaml"

inherit openautosar-app

RDEPENDS:${PN} += "openautosar-runtime openautosar-security"

do_install() {
    install -d ${D}${sysconfdir}/openautosar
    install -m 0644 \
        ${WORKDIR}/openautosar-time-network-policy.yaml \
        ${D}${sysconfdir}/openautosar/time-network-policy.yaml
}

FILES:${PN} += "${sysconfdir}/openautosar/time-network-policy.yaml"
