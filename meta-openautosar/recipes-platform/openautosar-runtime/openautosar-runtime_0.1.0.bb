# SPDX-License-Identifier: MIT

SUMMARY = "openAUTOSAR Adaptive runtime bootstrap"
DESCRIPTION = "C++20 host/runtime bootstrap and virtual vehicle smoke tools for qemux86-64 AGL images."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://openautosar-bootstrap.service \
           file://openautosar-bootstrap.env"

inherit openautosar-cluster openautosar-app useradd

DEPENDS += "openssl"

USERADD_PACKAGES = "${PN}"
GROUPADD_PARAM:${PN} = "--system openautosar"
USERADD_PARAM:${PN} = "--system --home /var/lib/openautosar --no-create-home --gid openautosar openautosar"

SYSTEMD_SERVICE:${PN} = "openautosar-bootstrap.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

do_install:append() {
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/openautosar-bootstrap.service ${D}${systemd_system_unitdir}/

    install -d ${D}${sysconfdir}/openautosar
    install -m 0644 ${WORKDIR}/openautosar-bootstrap.env ${D}${sysconfdir}/openautosar/bootstrap.env

    install -d ${D}${localstatedir}/lib/openautosar
    install -d ${D}${localstatedir}/log/openautosar
}

FILES:${PN} += "${localstatedir}/lib/openautosar ${localstatedir}/log/openautosar"
