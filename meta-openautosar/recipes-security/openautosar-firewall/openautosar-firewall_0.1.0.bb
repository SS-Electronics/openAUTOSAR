# SPDX-License-Identifier: MIT

SUMMARY = "openAUTOSAR firewall policy"
DESCRIPTION = "Default network firewall policy for qemux86-64 AGL images."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://openautosar-firewall-policy.yaml"

inherit openautosar-app

RDEPENDS:${PN} += "openautosar-runtime openautosar-security"

do_install() {
    install -d ${D}${sysconfdir}/openautosar
    install -m 0644 \
        ${WORKDIR}/openautosar-firewall-policy.yaml \
        ${D}${sysconfdir}/openautosar/firewall-policy.yaml
}

FILES:${PN} += "${sysconfdir}/openautosar/firewall-policy.yaml"
