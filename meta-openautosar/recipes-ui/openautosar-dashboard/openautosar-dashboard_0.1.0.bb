# SPDX-License-Identifier: MIT

SUMMARY = "openAUTOSAR Qt/QML engineering dashboard"
DESCRIPTION = "Diagnostic and health dashboard shell for qemux86-64 AGL images."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://openautosar-dashboard.service \
           file://openautosar-dashboard.env \
           file://openautosar-dashboard-launch.sh"

inherit openautosar-cluster openautosar-app

SYSTEMD_SERVICE:${PN} = "openautosar-dashboard.service"
SYSTEMD_AUTO_ENABLE:${PN} = "disable"

RDEPENDS:${PN} += "openautosar-runtime qtdeclarative-qmlplugins qtwayland"

do_install:append() {
    launch_script=${D}${bindir}/openautosar-dashboard-launch

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/openautosar-dashboard.service ${D}${systemd_system_unitdir}/

    install -d ${D}${sysconfdir}/openautosar
    install -m 0644 ${WORKDIR}/openautosar-dashboard.env ${D}${sysconfdir}/openautosar/dashboard.env
    install -d ${D}${bindir}
    install -m 0755 ${WORKDIR}/openautosar-dashboard-launch.sh ${launch_script}

    install -d ${D}${localstatedir}/lib/openautosar
    install -d ${D}${localstatedir}/log/openautosar
}

FILES:${PN} += "${datadir}/openautosar/dashboard"
FILES:${PN} += "${sysconfdir}/openautosar/dashboard.env"
FILES:${PN} += "${localstatedir}/lib/openautosar ${localstatedir}/log/openautosar"
