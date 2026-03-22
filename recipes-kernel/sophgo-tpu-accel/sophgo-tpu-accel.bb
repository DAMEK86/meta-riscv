SUMMARY = "Sophgo SG2000 TPU Accelerator - DRM/accel driver"
DESCRIPTION = "Linux accel subsystem driver for the 0.5 TOPS INT8 TPU \
found in Sophgo SG2000/CV1813H SoCs. Exposes /dev/accel/accelN with \
GEM buffer management, replacing the vendor's ION-based driver."
HOMEPAGE = "https://github.com/DAMEK86/meta-riscv"
SECTION = "kernel/modules"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://sophgo_tpu_drv.c;beginline=1;endline=2;md5=5f1db1a4251755b003fcaedee0489dba"

COMPATIBLE_MACHINE = "milkv-duo-s"

inherit module ptest

# ptest debug binary may contain build paths
INSANE_SKIP:${PN}-dbg += "buildpaths"

SRC_URI = " \
    file://sophgo-tpu-accel/Makefile \
    file://sophgo-tpu-accel/Kconfig \
    file://sophgo-tpu-accel/sophgo_tpu_drv.c \
    file://sophgo-tpu-accel/sophgo_tpu_drv.h \
    file://sophgo-tpu-accel/sophgo_tpu_gem.c \
    file://sophgo-tpu-accel/sophgo_tpu_submit.c \
    file://sophgo-tpu-accel/sophgo_tpu_hw.c \
    file://sophgo-tpu-accel/sophgo_tpu_hw.h \
    file://sophgo-tpu-accel/uapi/sophgo_tpu.h \
    file://tpu-test.c \
"

S = "${UNPACKDIR}/sophgo-tpu-accel"

RPROVIDES:${PN} += "kernel-module-sophgo-tpu-accel"

# Install UAPI header for userspace development
do_install:append() {
    install -d ${D}${includedir}/sophgo
    install -m 0644 ${S}/uapi/sophgo_tpu.h ${D}${includedir}/sophgo/
}

FILES:${PN}-dev += "${includedir}/sophgo/sophgo_tpu.h"

# ptest: build and install the test program
do_compile_ptest() {
    ${CC} ${CFLAGS} ${LDFLAGS} -I${STAGING_INCDIR} \
        ${UNPACKDIR}/tpu-test.c -o ${B}/tpu-test
}

do_install_ptest() {
    install -m 0755 ${B}/tpu-test ${D}${PTEST_PATH}/
    cat > ${D}${PTEST_PATH}/run-ptest << 'EOF'
#!/bin/sh
cd "$(dirname "$0")"
./tpu-test
EOF
    chmod 0755 ${D}${PTEST_PATH}/run-ptest
}

RDEPENDS:${PN}-ptest += "libdrm"
