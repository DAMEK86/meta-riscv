SUMMARY = "C++ library for reading/writing NumPy .npy and .npz files"
HOMEPAGE = "https://github.com/rogersce/cnpy"
SECTION = "libs"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=689f10b06d1ca2d4b1057e67b16cd580"

SRC_URI = "git://github.com/rogersce/cnpy.git;protocol=https;branch=master"
SRCREV = "4e8810b1a8637695171ed346ce68f6984e585ef4"

inherit cmake

DEPENDS = "zlib"

EXTRA_OECMAKE = "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"

do_install:append() {
    rm -rf ${D}${bindir}
}

FILES:${PN} = "${libdir}/libcnpy.so"
FILES:${PN}-dev = "${includedir} ${libdir}/cmake"
