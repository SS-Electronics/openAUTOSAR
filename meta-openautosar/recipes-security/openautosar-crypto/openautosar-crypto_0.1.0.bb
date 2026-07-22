# SPDX-License-Identifier: MIT

SUMMARY = "openAUTOSAR cryptography provider policy"
DESCRIPTION = "Crypto provider and key-slot policy for qemux86-64 AGL images."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://openautosar-crypto-policy.yaml"

inherit openautosar-app

RDEPENDS:${PN} += "openautosar-runtime openssl"

do_install() {
    install -d ${D}${sysconfdir}/openautosar
    install -m 0644 \
        ${WORKDIR}/openautosar-crypto-policy.yaml \
        ${D}${sysconfdir}/openautosar/crypto-policy.yaml
}

FILES:${PN} += "${sysconfdir}/openautosar/crypto-policy.yaml"
