SUMMARY = "Sophgo TPU inference examples for Milk-V Duo S"
DESCRIPTION = "Example programs demonstrating end-to-end TPU inference \
using the cviruntime DRM/accel API. Includes test_model_load which \
loads a .cvimodel file, enumerates input/output tensors, and runs a \
forward pass on the SG2000 TPU."
HOMEPAGE = "https://github.com/DAMEK86/meta-riscv"
SECTION = "examples"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://test_model_load.c"

DEPENDS = "cviruntime cvikernel"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} \
        -I${STAGING_INCDIR} \
        ${UNPACKDIR}/test_model_load.c \
        -L${STAGING_LIBDIR} \
        -lcviruntime -lcvikernel -lm \
        -o ${B}/test_model_load
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/test_model_load ${D}${bindir}/
}

FILES:${PN} = "${bindir}/test_model_load"

RDEPENDS:${PN} = "cviruntime cvikernel"

COMPATIBLE_MACHINE = "milkv-duo-s"
