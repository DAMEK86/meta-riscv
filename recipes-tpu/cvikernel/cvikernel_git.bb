SUMMARY = "Sophgo TPU kernel instruction library"
DESCRIPTION = "Generates TPU command buffers (TIU and TDMA instructions) \
for the Sophgo CV18xx/SG2000 TPU. Pure userspace computation, no kernel \
dependencies. Required by libcviruntime."
HOMEPAGE = "https://github.com/sophgo/cvikernel"
SECTION = "libs"
LICENSE = "CLOSED"

SRC_URI = "git://github.com/sophgo/cvikernel.git;protocol=https;branch=sg200x-dev"
SRCREV = "${AUTOREV}"

S = "${WORKDIR}/git"

inherit cmake

EXTRA_OECMAKE = " \
    -DCHIP=cv181x \
"

# The build produces both shared and static libraries
# cvikernel links only libm
DEPENDS = ""

do_install:append() {
    # Ensure headers are installed in standard location
    install -d ${D}${includedir}
    cp -R ${S}/include/* ${D}${includedir}/
}

FILES:${PN} = "${libdir}/libcvikernel.so"
FILES:${PN}-dev = "${includedir} ${libdir}/libcvikernel-static.a"
FILES:${PN}-staticdev = "${libdir}/libcvikernel-static.a"

COMPATIBLE_MACHINE = "milkv-duo-s"
