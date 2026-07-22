# SPDX-License-Identifier: MIT

SUMMARY = "openAUTOSAR runtime registry policy"
DESCRIPTION = "Basic machine, process, service, and software-cluster registry policy."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://openautosar-registry-policy.yaml"

inherit openautosar-app

RDEPENDS:${PN} += "openautosar-runtime"

do_install() {
    install -d ${D}${sysconfdir}/openautosar
    install -m 0644 \
        ${WORKDIR}/openautosar-registry-policy.yaml \
        ${D}${sysconfdir}/openautosar/registry-policy.yaml
}

FILES:${PN} += "${sysconfdir}/openautosar/registry-policy.yaml"
