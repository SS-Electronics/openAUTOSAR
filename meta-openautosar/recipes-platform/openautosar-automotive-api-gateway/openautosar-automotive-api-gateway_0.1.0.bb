# SPDX-License-Identifier: MIT

SUMMARY = "openAUTOSAR Automotive API Gateway policy"
DESCRIPTION = "Bounded non-AUTOSAR ingress mediation for service discovery and events."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://openautosar-automotive-api-gateway-policy.yaml"

inherit openautosar-app

RDEPENDS:${PN} += "openautosar-runtime openautosar-security"

do_install() {
    install -d ${D}${sysconfdir}/openautosar
    install -m 0644 \
        ${WORKDIR}/openautosar-automotive-api-gateway-policy.yaml \
        ${D}${sysconfdir}/openautosar/automotive-api-gateway-policy.yaml
}

FILES:${PN} += "${sysconfdir}/openautosar/automotive-api-gateway-policy.yaml"
