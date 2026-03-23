SUMMARY = "Sophgo TPU runtime library (ported to DRM/accel)"
DESCRIPTION = "Loads .cvimodel files, manages TPU memory via DRM/GEM, \
and submits command buffers to the Sophgo SG2000 TPU. Ported from \
vendor ION+chardev interface to upstream DRM/accel subsystem."
HOMEPAGE = "https://github.com/sophgo/cviruntime"
SECTION = "libs"
# Verify actual license file in repo. Use CLOSED if no LICENSE file exists.
LICENSE = "CLOSED"

SRC_URI = " \
    git://github.com/sophgo/cviruntime.git;protocol=https;branch=sg200x-dev \
"
SRCREV = "${AUTOREV}"

inherit cmake ptest

DEPENDS = "cvikernel cvibuilder flatbuffers lz4 libdrm sophgo-tpu-accel"

EXTRA_OECMAKE = " \
    -DCHIP=cv181x \
    -DRUNTIME=SOC \
    -DCVIKERNEL_PATH=${STAGING_DIR_TARGET}${prefix} \
    -DFLATBUFFERS_PATH=${STAGING_DIR_TARGET}${prefix} \
    -DCVIBUILDER_PATH=${STAGING_DIR_TARGET}${prefix} \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
"

# Skip building tool/ subdirectory (needs cnpy which we don't have)
do_configure:prepend() {
    sed -i '/add_subdirectory(tool)/d' ${S}/CMakeLists.txt
}

SRC_URI += "file://0001-port-to-drm-accel-uapi.patch"
SRC_URI += "file://cviruntime-test.c"

# Remove unwanted scripts installed by upstream CMake
do_install:append() {
    rm -f ${D}${prefix}/envs_tpu_sdk.sh
    rm -f ${D}${prefix}/regression_*.sh
}

FILES:${PN} = "${libdir}/libcviruntime.so"
FILES:${PN}-dev = "${includedir}"

COMPATIBLE_MACHINE = "milkv-duo-s"

do_compile_ptest() {
    # The ptest exercises DRM/GEM directly — no cviruntime linkage needed.
    # It only needs drm headers and the sophgo UAPI header.
    ${CC} ${CFLAGS} ${LDFLAGS} \
        -I${STAGING_INCDIR} \
        -I${STAGING_INCDIR}/drm \
        -I${STAGING_INCDIR}/sophgo \
        ${UNPACKDIR}/cviruntime-test.c \
        -lm \
        -o ${B}/cviruntime-test
}

do_install_ptest() {
    install -m 0755 ${B}/cviruntime-test ${D}${PTEST_PATH}/
    cat > ${D}${PTEST_PATH}/run-ptest << 'PTEOF'
#!/bin/sh
cd "$(dirname "$0")"
./cviruntime-test
PTEOF
    chmod 0755 ${D}${PTEST_PATH}/run-ptest
}

RDEPENDS:${PN}-ptest += "cvikernel libdrm"

# ptest debug binary may contain build paths
INSANE_SKIP:${PN}-dbg += "buildpaths"
