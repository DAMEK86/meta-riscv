# SPDX-License-Identifier: GPL-2.0

obj-m += sophgo-tpu-accel.o
sophgo-tpu-accel-y := sophgo_tpu_drv.o \
		      sophgo_tpu_gem.o \
		      sophgo_tpu_submit.o \
		      sophgo_tpu_hw.o

ccflags-y += -I$(src)/uapi

# Out-of-tree build
KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

.PHONY: all clean
