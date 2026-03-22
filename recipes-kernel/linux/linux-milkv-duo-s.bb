require linux-mainline-common.inc

SUMMARY = "Milk-V Duo S mainline kernel recipe"

LINUX_VERSION ?= "6.19.9"

BRANCH = "linux-6.19.y"
SRCREV = "v6.19.9"
SRC_URI = "git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git;protocol=https;branch=${BRANCH} \
           file://0001-riscv-dts-sophgo-add-sg2000-soc-and-milkv-duo-s.patch \
           file://0002-sophgo-add-cv1800-rtcsys-reset-handler.patch \
           file://0003-riscv-dts-sophgo-sg2000-add-tpu-node.patch \
           file://milkv-duo-s_defconfig \
           file://multi.its \
           "

KERNEL_FEATURES_RISCV = ""
KERNEL_DEVICETREE:milkv-duo-s ?= "sophgo/sg2000-milkv-duo-s.dtb"

DEPENDS = "u-boot-mkimage-native dtc-native"

do_deploy[depends] = "milkv-duo-fsbl:do_deploy"

do_deploy:append:milkv-duo-s() {
	cp ${B}/arch/riscv/boot/Image.gz ${B}
	cp ${UNPACKDIR}/multi.its ${B}
	mkimage -f ${B}/multi.its ${B}/uImage.fit
	install -m 744 ${B}/uImage.fit ${DEPLOYDIR}
	install -m 744 ${B}/arch/riscv/boot/dts/${KERNEL_DEVICETREE} ${DEPLOYDIR}/default.dtb
}

COMPATIBLE_MACHINE = "milkv-duo-s"
