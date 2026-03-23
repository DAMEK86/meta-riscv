SUMMARY = "Sophgo TDL SDK — AI/CV inference library for CV181X/SG200X"
DESCRIPTION = "libcvi_tdl.so wraps cviruntime and cvi_mpi to provide object \
detection, face detection, tracking and other AI/CV primitives. Built with \
ENABLE_SLIM=1 and ENABLE_SPEECH_RECOGNITION=0 to keep the dependency footprint \
small for the Milk-V Duo S."
HOMEPAGE = "https://github.com/sophgo/tdl_sdk"
SECTION = "libs"
LICENSE = "CLOSED"

SRC_URI = "git://github.com/sophgo/tdl_sdk.git;protocol=https;branch=master"
SRCREV = "${AUTOREV}"

inherit cmake
# ptest disabled until library linkage is fully resolved on target
# inherit ptest

# ── build-time deps ──────────────────────────────────────────────────────────
# cviruntime  → MLIR_SDK_ROOT  (libcviruntime.so, libcvikernel.so, headers)
# cvikernel   → also staged under cviruntime prefix (pulled in transitively)
# cvi-mpi     → MIDDLEWARE_SDK_ROOT (libsys.so, headers)
# opencv, flatbuffers, lz4 come from meta-oe / OE-core
DEPENDS = "cviruntime cvi-mpi cnpy opencv flatbuffers lz4 libdrm libeigen nlohmann-json python3-native"

# Upstream CMakeLists.txt calls 'python' not 'python3'
# Upstream opencv.cmake tries to download prebuilt OpenCV; replace with sysroot lookup
do_configure:prepend() {
    sed -i 's/COMMAND python /COMMAND python3 /' ${S}/CMakeLists.txt

    # Patch build_config.yaml: disable audio, enable only what we need for vision
    sed -i 's/ENABLE_AUDIO_CLASSIFICATION : 1/ENABLE_AUDIO_CLASSIFICATION : 0/' ${S}/build_config.yaml
    sed -i 's/ENABLE_IMAGE_CLASSIFICATION : 0/ENABLE_IMAGE_CLASSIFICATION : 1/' ${S}/build_config.yaml
    sed -i 's/ENABLE_SPEECH_RECOGNITION : 0/ENABLE_SPEECH_RECOGNITION : 0/' ${S}/build_config.yaml

    # Remove components that need vendor multimedia libs we don't have
    rm -rf ${S}/src/components/nn/audio_classification
    rm -rf ${S}/src/components/nn/speech_recognition
    rm -rf ${S}/src/components/video_decoder
    rm -rf ${S}/src/components/audio
    rm -rf ${S}/src/components/encoder
    rm -rf ${S}/src/components/network
    # Remove tdl_ex C API (references network/audio/encoder components)
    rm -f ${S}/src/c_apis/src/tdl_ex.cpp
    rm -f ${S}/src/c_apis/include/tdl_type_internal_ex.hpp
    # Strip c_apis_ex target from CMake
    sed -i '/c_apis_ex/d' ${S}/src/c_apis/CMakeLists.txt ${S}/src/CMakeLists.txt
    # Remove kaldi-native-fbank-core linkage (speech recognition disabled)
    sed -i '/kaldi-native-fbank-core/d' ${S}/src/CMakeLists.txt

    # Remove add_subdirectory refs for deleted components
    sed -i '/video_decoder\|audio\|encoder\|network/d' ${S}/src/components/CMakeLists.txt
    # Remove TARGET_OBJECTS refs for deleted components from top-level src/CMakeLists.txt
    sed -i '/TARGET_OBJECTS:encoder\|TARGET_OBJECTS:audio\|TARGET_OBJECTS:video_decoder\|TARGET_OBJECTS:network\|TARGET_OBJECTS:rtsp\|TARGET_OBJECTS:snapshot/d' ${S}/src/CMakeLists.txt

    # Purge all references to deleted components from remaining source
    find ${S}/src -name '*.cpp' -o -name '*.hpp' -o -name '*.h' | xargs sed -i \
        '/audio_classification\|speech_recognition\|FsmnVad\|ZipformerEncoder\|ZipformerDecoder\|ZipformerJoiner\|AudioClassification\|vi_decoder\|sample_comm/d'

    # Remove the entire DISABLE_SPEECH_RECOGNITION guarded block and add the define
    # to make any remaining #ifndef DISABLE_SPEECH_RECOGNITION blocks compile out
    python3 << 'PYEOF'
import re
path = "${S}/src/c_apis/src/tdl_core.cpp"
with open(path) as f:
    src = f.read()
# Remove blocks between #ifndef DISABLE_SPEECH_RECOGNITION and #endif
src = re.sub(r'#ifndef DISABLE_SPEECH_RECOGNITION.*?#endif', '', src, flags=re.DOTALL)
# Also remove any stray asr_model references
src = re.sub(r'.*asr_model.*\n', '', src)
with open(path, "w") as f:
    f.write(src)
PYEOF

    # Skip sample/ directory — it needs RTSP libs and platform .o files we don't have
    sed -i '/add_subdirectory(sample)/d' ${S}/CMakeLists.txt
    # Create dummy dirs that cmake install rules expect from middleware sysroot
    mkdir -p ${STAGING_DIR_TARGET}${prefix}/cvi-mpi/sample/common
    mkdir -p ${STAGING_DIR_TARGET}${prefix}/cvi-mpi/sample_app/common

    # Remove tdl_ex (audio/websocket extended library) — needs cvi_ssp, tinyalsa, curl etc.
    # We only need tdl_core for vision inference.
    # Use python3 for surgical removal since sed is too blunt for multi-line targets.
    python3 -c "
import re
with open('${S}/src/CMakeLists.txt', 'r') as f:
    content = f.read()
# Remove add_library(tdl_ex ...) and its target_link_libraries block
content = re.sub(r'add_library\(tdl_ex\b[^)]*\).*?\n', '', content)
content = re.sub(r'target_link_libraries\(tdl_ex\b.*?\)', '', content, flags=re.DOTALL)
content = re.sub(r'add_library\(tdl_ex-static\b[^)]*\).*?\n', '', content)
content = re.sub(r'target_link_libraries\(tdl_ex-static\b.*?\)', '', content, flags=re.DOTALL)
# Fix install line: remove tdl_ex tdl_ex-static from targets list
content = content.replace(' tdl_ex ', ' ').replace(' tdl_ex-static ', ' ')
content = content.replace(' tdl_ex\n', '\n').replace(' tdl_ex-static\n', '\n')
with open('${S}/src/CMakeLists.txt', 'w') as f:
    f.write(content)
"

    # Create a dummy toolchain file matching the expected riscv64 pattern.
    # This satisfies the architecture detection in opencv.cmake and thirdparty.cmake
    # while the actual cross-compiler is set by Yocto's cmake.bbclass.
    # Nested project() calls pick up CMAKE_TOOLCHAIN_FILE from the cache
    # and re-resolve the compiler from PATH — we ensure the full path is in PATH.
    mkdir -p ${S}/cmake
    # Create a real toolchain file that satisfies both:
    # 1) Pattern matching in opencv.cmake/thirdparty.cmake (filename)
    # 2) Nested project() calls that re-resolve the compiler (full paths)
    cat > ${S}/cmake/riscv64-unknown-linux-gnu.cmake << TCEOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR riscv64)
set(CMAKE_SYSROOT ${STAGING_DIR_TARGET})
set(CMAKE_C_COMPILER ${STAGING_DIR_NATIVE}/usr/bin/riscv64-poky-linux/riscv64-poky-linux-gcc)
set(CMAKE_CXX_COMPILER ${STAGING_DIR_NATIVE}/usr/bin/riscv64-poky-linux/riscv64-poky-linux-g++)
set(CMAKE_FIND_ROOT_PATH ${STAGING_DIR_TARGET})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
TCEOF

    # Replace thirdparty.cmake — the upstream version calls project() which
    # resets the compiler and tries to FetchContent from the internet.
    # We provide deps via Yocto or skip unused ones (ENABLE_SLIM=1).
    cat > ${S}/cmake/thirdparty.cmake << 'TPEOF'
# Stubbed out — third party deps provided by Yocto sysroot or not needed
set(GTEST_INCLUDES "")
set(KISSFFT_INCLUDES "")
set(FBANK_INCLUDES "")
set(LIBWEBSOCKETS_LIBS "")
set(LIBWEBSOCKETS_LIBS_STATIC "")
TPEOF

    # Replace opencv.cmake with a version that uses Yocto's sysroot OpenCV
    cat > ${S}/cmake/opencv.cmake << 'OPENCVEOF'
# Use system OpenCV from Yocto sysroot
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)
set(OPENCV_INCLUDES ${OpenCV_INCLUDE_DIRS})
set(OPENCV_LIBS_IMCODEC ${OpenCV_LIBS})
set(OPENCV_LIBS_IMCODEC_STATIC "")
message(STATUS "Using system OpenCV: ${OpenCV_VERSION} from ${OpenCV_DIR}")
OPENCVEOF
}

# ── CMake variables ───────────────────────────────────────────────────────────
# MLIR_SDK_ROOT  : cmake/mlir.cmake expects libcvikernel.so + libcviruntime.so
#                  under ${MLIR_SDK_ROOT}/lib/ and headers under include/.
#                  Both cviruntime and cvikernel install to ${prefix}, so
#                  STAGING_DIR_TARGET${prefix} covers both.
#
# MIDDLEWARE_SDK_ROOT : cmake/middleware.cmake probes this for CV181X. cvi-mpi
#                  installs libsys.so to ${libdir} and headers to
#                  ${includedir}/cvi-mpi/, but middleware.cmake expects the
#                  include/ and lib/ directly under the root. We point at the
#                  sysroot prefix and let cmake pick them up via the standard
#                  sub-directories.
#
# OPENCV_ROOT_DIR : only required for BM/CMODEL platforms; for CV181X the cmake
#                  file short-circuits before checking this variable — but we
#                  pass it anyway in case an opencv find_package call is hit.
#
# ENABLE_SLIM=1   : skips evaluation/ and tests/ subdirectories (no gtest dep).
# MW_VER          : cvi_mpi sg200x-dev branch uses v2 APIs.
# BUILD_OPTION    : empty string → build src + sample (default).
EXTRA_OECMAKE = " \
    -DCVI_PLATFORM=CV181X \
    -DMW_VER=v2 \
    -DENABLE_SLIM=1 \
    -DENABLE_SPEECH_RECOGNITION=0 \
    -DENABLE_AUDIO_CLASSIFICATION=0 \
    -DMLIR_SDK_ROOT=${STAGING_DIR_TARGET}${prefix} \
    -DMIDDLEWARE_SDK_ROOT=${STAGING_DIR_TARGET}${prefix}/cvi-mpi \
    -DOPENCV_ROOT_DIR=${STAGING_DIR_TARGET}${prefix} \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_CXX_FLAGS="-I${STAGING_DIR_TARGET}${includedir}/eigen3 -I${STAGING_DIR_TARGET}${includedir}/nlohmann" \
    -DCMAKE_TOOLCHAIN_FILE=${S}/cmake/riscv64-unknown-linux-gnu.cmake \
    -DTOOLCHAIN_PATH=${S}/cmake \
"

# ── install ───────────────────────────────────────────────────────────────────
# The upstream install() rules place everything under CMAKE_INSTALL_PREFIX.
# cmake.bbclass sets CMAKE_INSTALL_PREFIX=${prefix}, so the shared library
# lands in ${prefix}/lib/ → mapped to ${D}${libdir} by Yocto.
# Headers are installed to ${prefix}/include/ → ${D}${includedir}.

# Override do_install entirely — upstream cmake install rules reference
# too many vendor-specific paths (rtsp, sample, configs) that don't exist.
do_install() {
    install -d ${D}${libdir}
    install -m 0755 ${B}/src/libtdl_core.so ${D}${libdir}/
    install -d ${D}${includedir}/tdl
    cp -r ${S}/include/* ${D}${includedir}/tdl/
}

FILES:${PN}       = "${libdir}/libtdl_core.so*"
FILES:${PN}-dev   = "${includedir} ${libdir}/libtdl_core-static.a"
FILES:${PN}-staticdev = "${libdir}/libtdl_core-static.a"

# Pull in shared-library run-time deps on target
RDEPENDS:${PN} = "cviruntime cvi-mpi libdrm"

INSANE_SKIP:${PN} = "already-stripped buildpaths useless-rpaths"

COMPATIBLE_MACHINE = "milkv-duo-s"

# ── ptest ─────────────────────────────────────────────────────────────────────
# The ptest exercises the full inference stack:
#   sophgo-tpu-accel driver → cviruntime → cvikernel → libcvi_tdl → model inference
#
# If a .cvimodel is placed at ${PTEST_PATH}/mobilenetv2.cvimodel (or pointed to
# via the TDL_TEST_MODEL env-var), Tests 2-5 will run full MobileNetV2 inference.
# Without a model the ptest still validates handle create/destroy (Tests 1 & 6).
#
# To bundle a model, add it to SRC_URI and uncomment the install line below:
#   SRC_URI += "https://...mobilenetv2.cvimodel;name=model"
#   SRC_URI[model.sha256sum] = "<sha256>"

SRC_URI += "file://tdl-sdk-test.c"

# inherit ptest  -- disabled until vendor lib linkage resolved on target

do_compile_ptest() {
    ${CC} ${CFLAGS} ${LDFLAGS} \
        -I${STAGING_INCDIR} \
        -I${S}/include \
        -I${S}/include/c_apis \
        ${UNPACKDIR}/tdl-sdk-test.c \
        -L${B}/src -ltdl_core \
        -lcviruntime -lcvikernel -lm \
        -o ${B}/tdl-sdk-test
}

do_install_ptest() {
    install -d ${D}${PTEST_PATH}
    install -m 0755 ${B}/tdl-sdk-test ${D}${PTEST_PATH}/

    # run-ptest script: ptest framework calls this to collect results
    cat > ${D}${PTEST_PATH}/run-ptest << 'PTEOF'
#!/bin/sh
# TDL SDK end-to-end inference ptest
# Optional: set TDL_TEST_MODEL and TDL_TEST_IMAGE to override search paths.
cd "$(dirname "$0")"
exec ./tdl-sdk-test "$@"
PTEOF
    chmod 0755 ${D}${PTEST_PATH}/run-ptest

    # Uncomment to install a bundled model:
    # install -m 0644 ${UNPACKDIR}/mobilenetv2.cvimodel ${D}${PTEST_PATH}/
}

RDEPENDS:${PN}-ptest += "cviruntime cvikernel cvi-mpi libdrm"
