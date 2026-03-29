require linux-milkv-common.inc

SUMMARY = "Milk-V Duo family mainline kernel recipe"

LINUX_VERSION ?= "6.19.9"
BRANCH ?= "linux-6.19.y"
SRCREV ?= "v6.19.9"

# Patches live in sibling recipe directories; add them to FILESPATH
# linux-milkv-duo-s has split patches (0001-0005)
# linux-milkv-duo has duo-specific patches
FILESEXTRAPATHS:prepend := "${THISDIR}/linux-milkv-duo-family:"

SRC_URI = "git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git;protocol=https;branch=${BRANCH} \
    file://0001-riscv-dts-sophgo-add-sg2000-dtsi.patch \
    file://0002-riscv-dts-sophgo-add-milkv-duo-s.patch \
    file://0003-riscv-dts-sophgo-add-milkv-duo-256m.patch \
    file://0004-sophgo-add-cv1800-rtcsys-reset-handler.patch \
    file://0005-mmc-sdhci-of-dwcmshc-add-cv18xx-callbacks-for-SG2000-eMMC.patch \
    file://dts-exclude-memory-occupied-by-opensbi.patch \
    file://${MACHINE}_defconfig \
    file://multi.its \
    ${@bb.utils.contains('MACHINE_FEATURES', 'wifi', 'file://wifi.cfg', '', d)}"

COMPATIBLE_MACHINE = "milkv-(duo|duo-s|duo256m)"
