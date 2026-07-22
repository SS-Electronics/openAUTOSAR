# SPDX-License-Identifier: MIT

SUMMARY:${PN} ?= "openAUTOSAR Adaptive application"
LICENSE ?= "MIT"
LIC_FILES_CHKSUM ?= "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit systemd

SYSTEMD_AUTO_ENABLE:${PN} ?= "disable"

FILES:${PN} += "${systemd_system_unitdir} ${sysconfdir}/openautosar"
