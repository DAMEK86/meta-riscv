SUMMARY = "Sophgo SG2000 TPU inference tests and examples"
DESCRIPTION = "Ptests, examples, and validation tools for the TPU inference stack."

inherit packagegroup

RDEPENDS:${PN} = " \
    packagegroup-tpu-inference \
    sophgo-tpu-accel-ptest \
    cviruntime-ptest \
    tpu-examples \
"
