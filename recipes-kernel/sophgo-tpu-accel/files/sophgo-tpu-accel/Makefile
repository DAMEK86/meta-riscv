# SPDX-License-Identifier: GPL-2.0

obj-m += sophgo-tpu-accel.o
sophgo-tpu-accel-y := sophgo_tpu_drv.o \
		      sophgo_tpu_gem.o \
		      sophgo_tpu_submit.o \
		      sophgo_tpu_hw.o

ccflags-y += -I$(src)/uapi

# Out-of-tree build
ifdef KERNEL_SRC
KDIR := $(KERNEL_SRC)
else
KDIR ?= /lib/modules/$(shell uname -r)/build
endif

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

modules_install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

.PHONY: all modules_install clean
