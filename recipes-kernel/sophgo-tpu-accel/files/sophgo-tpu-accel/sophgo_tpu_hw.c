// SPDX-License-Identifier: GPL-2.0
/*
 * Sophgo SG2000 TPU Accelerator - Hardware abstraction
 *
 * Based on sophgo/osdrv interdrv/tpu/hal/cv181x/tpu_platform.c
 * Adapted for DRM/accel subsystem (no ION, no TEE).
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/reset.h>
#include <linux/dma-mapping.h>
#include <asm/cacheflush.h>

#include "sophgo_tpu_drv.h"
#include "sophgo_tpu_hw.h"

/*
 * DMA buffer header — placed at the start of every command buffer.
 * Describes the TPU work to be done (TIU + TDMA descriptors).
 * This struct must match the format produced by libcvikernel.
 */
struct dma_hdr {
	u16 magic_m;
	u16 magic_s;
	u32 size;
	u32 cpu_desc_count;
	u32 bd_desc_count;
	u32 tdma_desc_count;
	u32 tpu_clk_rate;
	u32 pmubuf_size;
	u32 pmubuf_offset;
	/* Array base addresses (8 × 64-bit) */
	u32 arraybase[16];
	u32 reserved[8];
} __packed;

static void sophgo_tpu_hw_power_on(struct sophgo_tpu_device *tdev)
{
	clk_prepare_enable(tdev->clk_axi);
	clk_prepare_enable(tdev->clk_fab);
}

static void sophgo_tpu_hw_power_off(struct sophgo_tpu_device *tdev)
{
	clk_disable_unprepare(tdev->clk_axi);
	clk_disable_unprepare(tdev->clk_fab);
}

int sophgo_tpu_hw_reset(struct sophgo_tpu_device *tdev)
{
	reset_control_assert(tdev->rst_tdma);
	reset_control_assert(tdev->rst_tpu);
	reset_control_assert(tdev->rst_tpusys);

	udelay(10);

	reset_control_deassert(tdev->rst_tdma);
	reset_control_deassert(tdev->rst_tpu);
	reset_control_deassert(tdev->rst_tpusys);

	udelay(10);

	return 0;
}

int sophgo_tpu_hw_init(struct sophgo_tpu_device *tdev)
{
	int ret;

	sophgo_tpu_hw_power_on(tdev);

	ret = sophgo_tpu_hw_reset(tdev);
	if (ret) {
		sophgo_tpu_hw_power_off(tdev);
		return ret;
	}

	/* INT_MASK is inverted: writing a bit MASKS that interrupt.
	 * 0x20 = mask only the stride=0 error, leaving EOD unmasked.
	 * (vendor: TDMA_MASK_INIT = 0x20)
	 */
	writel(0x20, tdev->tdma_regs + TDMA_INT_MASK);

	dev_dbg(tdev->dev, "TPU HW init: AXI clk=%lu Hz\n",
		clk_get_rate(tdev->clk_axi));

	return 0;
}

void sophgo_tpu_hw_fini(struct sophgo_tpu_device *tdev)
{
	/* Reset and power off */
	writel(0, tdev->tdma_regs + TDMA_CTRL);
	sophgo_tpu_hw_power_off(tdev);
}

/*
 * Submit a DMA buffer to the TPU hardware.
 *
 * The dmabuf contains a header followed by TIU and TDMA descriptors.
 * We configure the TDMA engine with array base addresses and descriptor
 * base, then kick execution. The TDMA completion IRQ signals when done.
 *
 * Based on vendor platform_run_dmabuf() from tpu_platform.c
 */
int sophgo_tpu_hw_run_dmabuf(struct sophgo_tpu_device *tdev,
			     void *dmabuf_vaddr, dma_addr_t dmabuf_paddr)
{
	struct dma_hdr *header = dmabuf_vaddr;
	u32 num_tdma;

	/* Validate header magic */
	if (header->magic_m != TPU_DMABUF_HEADER_M) {
		dev_err(tdev->dev, "invalid dmabuf magic: 0x%04x (expected 0x%04x)\n",
			header->magic_m, TPU_DMABUF_HEADER_M);
		return -EINVAL;
	}

	num_tdma = header->tdma_desc_count;

	/* Store clock rate in header for PMU */
	header->tpu_clk_rate = clk_get_rate(tdev->clk_axi);

	/* Set up array base addresses from header */
	writel(header->arraybase[0], tdev->tdma_regs + TDMA_ARRAYBASE0_L);
	writel(header->arraybase[1], tdev->tdma_regs + TDMA_ARRAYBASE0_H);
	writel(header->arraybase[2], tdev->tdma_regs + TDMA_ARRAYBASE1_L);
	writel(header->arraybase[3], tdev->tdma_regs + TDMA_ARRAYBASE1_H);
	writel(header->arraybase[4], tdev->tdma_regs + TDMA_ARRAYBASE2_L);
	writel(header->arraybase[6], tdev->tdma_regs + TDMA_ARRAYBASE3_L);
	writel(header->arraybase[8], tdev->tdma_regs + TDMA_ARRAYBASE4_L);
	writel(header->arraybase[10], tdev->tdma_regs + TDMA_ARRAYBASE5_L);
	writel(header->arraybase[12], tdev->tdma_regs + TDMA_ARRAYBASE6_L);
	writel(header->arraybase[14], tdev->tdma_regs + TDMA_ARRAYBASE7_L);

	/* Set TIU descriptor base address */
	writel(header->arraybase[0], tdev->tiu_regs + BD_CTRL_BASE_ADDR);

	/* Set TDMA descriptor base address (byte offset within buffer) */
	{
		u32 *cpu_desc = (u32 *)((u8 *)dmabuf_vaddr + 128);
		u32 tdma_offset = cpu_desc[3]; /* offset_tdma */
		writel(tdma_offset, tdev->tdma_regs + TDMA_DES_BASE);
	}

	/* Disable debug mode and enable DCM (vendor sequence) */
	writel(0x0, tdev->tdma_regs + TDMA_DEBUG_MODE);
	writel(0x0, tdev->tdma_regs + TDMA_DCM_DISABLE);

	/* Set interrupt mask: 0x20 = mask only stride=0 error */
	writel(0x20, tdev->tdma_regs + TDMA_INT_MASK);

	/* Reset completion before kicking */
	reinit_completion(&tdev->tdma_done);

	/* Configure and kick TDMA engine (descriptor mode) — non-blocking */
	writel((1 << TDMA_CTRL_ENABLE_BIT) |
	       (1 << TDMA_CTRL_MODESEL_BIT) |
	       (num_tdma << TDMA_CTRL_DESNUM_BIT) |
	       (3 << TDMA_CTRL_BURSTLEN_BIT) |
	       (1 << TDMA_CTRL_FORCE_1ARRAY) |
	       (1 << TDMA_CTRL_INTRA_CMD_OFF) |
	       (1 << TDMA_CTRL_64BYTE_ALIGN_EN),
	       tdev->tdma_regs + TDMA_CTRL);

	/* Returns immediately — caller uses wait ioctl for completion */
	return 0;
}

/*
 * TDMA completion IRQ handler.
 *
 * Based on vendor platform_tdma_irq() from tpu_platform.c
 */
irqreturn_t sophgo_tpu_hw_irq(int irq, void *data)
{
	struct sophgo_tpu_device *tdev = data;
	u32 reg_value, int_status;

	/* Read interrupt status from upper 16 bits of INT_MASK register */
	reg_value = readl(tdev->tdma_regs + TDMA_INT_MASK);
	int_status = (reg_value >> 16) & ~0x20; /* mask out stride error bit */

	if (int_status != TDMA_INT_EOD)
		dev_err(tdev->dev, "TDMA IRQ unexpected: reg=0x%08x status=0x%x\n",
			reg_value, int_status);

	/* Mask all interrupts to acknowledge (vendor: write 0xFFFF0000) */
	writel(0xFFFF0000, tdev->tdma_regs + TDMA_INT_MASK);

	complete(&tdev->tdma_done);

	return IRQ_HANDLED;
}
