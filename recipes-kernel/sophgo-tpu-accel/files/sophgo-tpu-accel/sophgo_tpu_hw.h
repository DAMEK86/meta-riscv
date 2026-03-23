/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Sophgo SG2000 TPU - Hardware register definitions
 *
 * Derived from sophgo/osdrv interdrv/tpu/hal/cv181x/
 * Register offsets for TDMA and TIU blocks.
 */

#ifndef __SOPHGO_TPU_HW_H__
#define __SOPHGO_TPU_HW_H__

/* TDMA register offsets (from TDMA base 0x0C100000) */
#define TDMA_CTRL			0x000
#define TDMA_DES_BASE			0x004
#define TDMA_INT_MASK			0x008
#define TDMA_SYNC_STATUS		0x00C
#define TDMA_CMD_ACCP0			0x010
#define TDMA_CMD_ACCP1			0x014
#define TDMA_CMD_ACCP2			0x018
#define TDMA_CMD_ACCP3			0x01C
#define TDMA_ARRAYBASE0_L		0x020
#define TDMA_ARRAYBASE0_H		0x024
#define TDMA_ARRAYBASE1_L		0x028
#define TDMA_ARRAYBASE1_H		0x02C
#define TDMA_ARRAYBASE2_L		0x030
#define TDMA_ARRAYBASE3_L		0x038
#define TDMA_ARRAYBASE4_L		0x040
#define TDMA_ARRAYBASE5_L		0x048
#define TDMA_ARRAYBASE6_L		0x050
#define TDMA_ARRAYBASE7_L		0x058
#define TDMA_DEBUG_MODE			0x060
#define TDMA_DCM_DISABLE		0x064

/* TDMA_CTRL bit positions */
#define TDMA_CTRL_ENABLE_BIT		0
#define TDMA_CTRL_MODESEL_BIT		1  /* 0=PIO, 1=descriptor */
#define TDMA_CTRL_RESET_SYNCID_BIT	4
#define TDMA_CTRL_FORCE_1ARRAY		5
#define TDMA_CTRL_DESNUM_BIT		16
#define TDMA_CTRL_BURSTLEN_BIT		8
#define TDMA_CTRL_64BYTE_ALIGN_EN	12
#define TDMA_CTRL_INTRA_CMD_OFF		13

/* TDMA interrupt status bits (upper 16 bits of INT_MASK register) */
#define TDMA_INT_EOD			(1 << 0)  /* End of single descriptor */
#define TDMA_INT_CMDQ_EMPTY		(1 << 9)  /* Command queue empty (all descriptors done) */
#define TDMA_INT_VALID_MASK		(TDMA_INT_EOD | TDMA_INT_CMDQ_EMPTY)
#define TDMA_INT_ERROR			(1 << 16) /* Error flag */

/* TIU (Tensor Instruction Unit) register offsets */
#define BD_CTRL_BASE_ADDR		0x000
#define BD_MAIN_CTRL			0x004

/* DMA buffer header magic (vendor-defined) */
#define TPU_DMABUF_HEADER_M		0xB5B5

/* Timeout for TPU operations */
#define SOPHGO_TPU_TIMEOUT_MS		10000  /* 10 seconds */

#endif /* __SOPHGO_TPU_HW_H__ */
