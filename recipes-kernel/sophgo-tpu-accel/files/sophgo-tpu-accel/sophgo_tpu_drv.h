/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Sophgo SG2000 TPU Accelerator Driver
 *
 * Copyright (C) 2026 meta-riscv contributors
 * Based on Sophgo vendor driver from sophgo/osdrv (GPL-2.0)
 */

#ifndef __SOPHGO_TPU_DRV_H__
#define __SOPHGO_TPU_DRV_H__

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/reset.h>
#include <linux/spinlock.h>
#include <drm/drm_device.h>
#include <drm/drm_gem.h>

#define SOPHGO_TPU_DRIVER_NAME	"sophgo-tpu"
#define SOPHGO_TPU_DRIVER_DESC	"Sophgo SG2000 TPU Accelerator"
#define SOPHGO_TPU_DRIVER_MAJOR	0
#define SOPHGO_TPU_DRIVER_MINOR	1

/**
 * struct sophgo_tpu_device - Top-level driver state
 * @drm:          DRM device (embedded, accel subsystem)
 * @dev:          Platform device
 * @tdma_regs:    TDMA register block (ioremap'd)
 * @tiu_regs:     TIU register block (ioremap'd)
 * @clk_axi:      TPU AXI clock
 * @clk_fab:      TPU fabric clock
 * @rst_tdma:     TDMA reset control
 * @rst_tpu:      TPU reset control
 * @rst_tpusys:   TPU subsystem reset control
 * @tdma_irq:     TDMA completion interrupt number
 * @tdma_done:    Completion for TDMA interrupt
 * @submit_lock:  Serialises TPU job submission
 * @seq_no:       Monotonic sequence number for job tracking
 */
struct sophgo_tpu_device {
	struct drm_device drm;
	struct device *dev;

	/* MMIO */
	void __iomem *tdma_regs;
	void __iomem *tiu_regs;

	/* Clocks */
	struct clk *clk_axi;
	struct clk *clk_fab;

	/* Resets */
	struct reset_control *rst_tdma;
	struct reset_control *rst_tpu;
	struct reset_control *rst_tpusys;

	/* IRQ */
	int tdma_irq;
	struct completion tdma_done;

	/* Submission */
	struct mutex submit_lock;
	u32 seq_no;
};

static inline struct sophgo_tpu_device *to_sophgo_tpu(struct drm_device *drm)
{
	return container_of(drm, struct sophgo_tpu_device, drm);
}

/**
 * struct sophgo_tpu_bo - GEM buffer object for TPU
 * @base:   DRM GEM object
 * @paddr:  Physical/DMA address
 * @vaddr:  Kernel virtual address (for cache ops)
 * @size:   Allocated size
 */
struct sophgo_tpu_bo {
	struct drm_gem_object base;
	dma_addr_t paddr;
	void *vaddr;
	size_t size;
};

static inline struct sophgo_tpu_bo *to_sophgo_tpu_bo(struct drm_gem_object *obj)
{
	return container_of(obj, struct sophgo_tpu_bo, base);
}

/* sophgo_tpu_gem.c */
int sophgo_tpu_create_bo_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file);
int sophgo_tpu_mmap_bo_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file);
int sophgo_tpu_sync_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file);
extern const struct drm_gem_object_funcs sophgo_tpu_gem_funcs;

/* sophgo_tpu_submit.c */
int sophgo_tpu_submit_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file);
int sophgo_tpu_wait_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file);

/* sophgo_tpu_hw.c */
int sophgo_tpu_hw_init(struct sophgo_tpu_device *tdev);
void sophgo_tpu_hw_fini(struct sophgo_tpu_device *tdev);
int sophgo_tpu_hw_reset(struct sophgo_tpu_device *tdev);
int sophgo_tpu_hw_run_dmabuf(struct sophgo_tpu_device *tdev,
			     void *dmabuf_vaddr, dma_addr_t dmabuf_paddr);
irqreturn_t sophgo_tpu_hw_irq(int irq, void *data);

#endif /* __SOPHGO_TPU_DRV_H__ */
