# SPDX-License-Identifier: MIT

SUMMARY = "openAUTOSAR security policy and IAM configuration"
DESCRIPTION = "Default IAM, signer-trust, and IDSM policy for qemux86-64 AGL images."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://openautosar-security-policy.yaml"

inherit openautosar-app

RDEPENDS:${PN} += "openautosar-runtime"

do_install() {
    install -d ${D}${sysconfdir}/openautosar
    install -m 0644 \
        ${WORKDIR}/openautosar-security-policy.yaml \
        ${D}${sysconfdir}/openautosar/security-policy.yaml
}

FILES:${PN} += "${sysconfdir}/openautosar/security-policy.yaml"
