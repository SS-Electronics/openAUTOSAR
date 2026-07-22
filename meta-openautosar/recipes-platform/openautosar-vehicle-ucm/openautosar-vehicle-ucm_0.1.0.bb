# SPDX-License-Identifier: MIT

SUMMARY = "openAUTOSAR Vehicle UCM policy"
DESCRIPTION = "Vehicle-wide update campaign coordination policy for QEMU deployments."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://openautosar-vehicle-ucm-policy.yaml"

inherit openautosar-app

RDEPENDS:${PN} += "openautosar-runtime openautosar-security"

do_install() {
    install -d ${D}${sysconfdir}/openautosar
    install -m 0644 \
        ${WORKDIR}/openautosar-vehicle-ucm-policy.yaml \
        ${D}${sysconfdir}/openautosar/vehicle-ucm-policy.yaml
}

FILES:${PN} += "${sysconfdir}/openautosar/vehicle-ucm-policy.yaml"
