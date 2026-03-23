SUMMARY = "Sophgo CVI model flatbuffers schema headers"
DESCRIPTION = "Generates C++ headers from .fbs schema files for cvimodel \
and parameter formats. These headers are required by cviruntime to parse \
.cvimodel files."
HOMEPAGE = "https://github.com/sophgo/cvibuilder"
SECTION = "devel"
LICENSE = "CLOSED"

SRC_URI = "git://github.com/sophgo/cvibuilder.git;protocol=https;branch=sg200x-dev"
SRCREV = "${AUTOREV}"

inherit cmake

# flatc (the compiler) runs on the build host, so we need flatbuffers-native
DEPENDS = "flatbuffers-native"

# Point at the native flatc binary
EXTRA_OECMAKE = " \
    -DFLATBUFFERS_PATH=${STAGING_DIR_NATIVE}${prefix} \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
"

# This recipe only produces headers — no shared libraries
FILES:${PN}-dev = "${includedir}/cvibuilder"
ALLOW_EMPTY:${PN} = "1"

COMPATIBLE_MACHINE = "milkv-duo-s"
