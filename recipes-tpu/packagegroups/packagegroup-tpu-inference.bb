SUMMARY = "Sophgo SG2000 TPU inference stack"
DESCRIPTION = "Core TPU inference pipeline for the Sophgo SG2000/CV1813H: \
kernel driver, runtime libraries, and vision inference SDK."

inherit packagegroup

RDEPENDS:${PN} = " \
    kernel-module-sophgo-tpu-accel \
    cvikernel \
    cviruntime \
    cvi-mpi \
    tdl-sdk \
"
