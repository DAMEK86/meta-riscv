SUMMARY = "Sophgo MPI middleware — libsys only"
DESCRIPTION = "Minimal build of cvi_mpi modules/sys, producing libsys.so and \
libsys.a. Provides the system/VB initialisation API required by TDL SDK. \
The full multimedia stack (VI, VO, VPSS, ISP, audio …) is intentionally \
excluded."
HOMEPAGE = "https://github.com/sophgo/cvi_mpi"
SECTION = "libs"
LICENSE = "CLOSED"

SRC_URI = "git://github.com/sophgo/cvi_mpi.git;protocol=https;branch=sg200x-dev \
    file://0001-fix-atomic-var-init-gcc15.patch \
"
SRCREV = "${AUTOREV}"

COMPATIBLE_MACHINE = "milkv-duo-s"

# cvi_mpi uses CROSS_COMPILE-prefixed toolchain invocations directly; the
# Yocto SDK toolchain exports CC, AR, etc. but the Makefile reads them via
# CROSS_COMPILE, so we derive it from the CC variable.
CROSS_COMPILE = "${TARGET_PREFIX}"

# Chip/arch selection for SG200X (covers CV1813H / Milk-V Duo S)
CHIP_ARCH = "SG200X"

# Output directories: build inside WORKDIR to avoid polluting S
MW_LIB = "${WORKDIR}/mw_lib"
MW_INC_DIR = "${WORKDIR}/mw_inc"

# mpi_param.mk picks up KERNEL_INC for a handful of uapi headers.
# Point it at the staging kernel headers supplied by the SDK.
KERNEL_INC = "${STAGING_KERNEL_BUILDDIR}/usr/include"

do_compile() {
    # Create output directories that the Makefile expects to exist.
    mkdir -p "${MW_LIB}"
    mkdir -p "${MW_INC_DIR}"

    # Copy top-level public headers so that INCS = -I$(MW_INC) resolves.
    cp -r "${S}/include/." "${MW_INC_DIR}/"

    # Strip out sources that pull in external dependencies (GDC math, grid)
    sed -i '/gdc_mesh\.c/d;/grid_info\.c/d' "${S}/modules/sys/Makefile"

    # Build only modules/sys — invoke its Makefile directly, passing all
    # required variables.  PARAM_FILE is set so it does not fall back to
    # ../../mpi_param.mk (which requires the full repo environment).
    oe_runmake -C "${S}/modules/sys" \
        PARAM_FILE="${S}/mpi_param.mk" \
        CROSS_COMPILE="${CROSS_COMPILE}" \
        CHIP_ARCH="${CHIP_ARCH}" \
        MW_PATH="${S}" \
        MW_LIB="${MW_LIB}" \
        MW_3RD_LIB="${MW_LIB}" \
        MW_INC="${MW_INC_DIR}" \
        SYS_INC="${S}/modules/sys/include" \
        KERNEL_INC="${STAGING_KERNEL_BUILDDIR}/usr/include" \
        CC="${CC}" \
        AR="${AR}" \
        LD="${LD}" \
        STRIP="${STRIP}"
}

do_install() {
    # ── libraries ──────────────────────────────────────────────────────────
    install -d "${D}${libdir}"
    install -m 0755 "${MW_LIB}/libsys.so"  "${D}${libdir}/"
    install -m 0644 "${MW_LIB}/libsys.a"   "${D}${libdir}/"

    # ── public headers ─────────────────────────────────────────────────────
    # Install to a private prefix to avoid polluting the global namespace.
    # cvi_mpi headers conflict with glibc/GCC 15 system headers if installed
    # directly to /usr/include/. TDL SDK uses MIDDLEWARE_SDK_ROOT to find them.
    install -d "${D}${prefix}/cvi-mpi/include"
    cp -r "${S}/include/." "${D}${prefix}/cvi-mpi/include/"
    cp -r "${S}/modules/sys/include/." "${D}${prefix}/cvi-mpi/include/"
    # TDL SDK also looks for libs under MIDDLEWARE_SDK_ROOT/lib/
    install -d "${D}${prefix}/cvi-mpi/lib"
    ln -sf ${libdir}/libsys.so "${D}${prefix}/cvi-mpi/lib/libsys.so"
}

# ── packaging ──────────────────────────────────────────────────────────────
FILES:${PN}          = "${libdir}/libsys.so"
FILES:${PN}-dev      = "${prefix}/cvi-mpi ${libdir}/libsys.a"
FILES:${PN}-staticdev = "${libdir}/libsys.a"

# libsys links only against libc / libm — no extra DEPENDS needed.
DEPENDS = "virtual/kernel"

# Stage the private cvi-mpi prefix into consumers' recipe-sysroot
SYSROOT_DIRS += "${prefix}/cvi-mpi"

INSANE_SKIP:${PN} = "already-stripped ldflags"
INSANE_SKIP:${PN}-dbg = "buildpaths"
INSANE_SKIP:${PN}-staticdev = "buildpaths"
