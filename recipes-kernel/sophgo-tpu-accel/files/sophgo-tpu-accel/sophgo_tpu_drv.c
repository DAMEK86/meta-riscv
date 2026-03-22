// SPDX-License-Identifier: GPL-2.0
/*
 * Sophgo SG2000 TPU Accelerator - DRM/accel driver
 *
 * Copyright (C) 2026 meta-riscv contributors
 * Based on Sophgo vendor driver from sophgo/osdrv (GPL-2.0)
 *
 * This driver exposes the SG2000's 0.5 TOPS INT8 TPU via the Linux
 * accel subsystem (/dev/accel/accelN). It replaces the vendor's custom
 * char device (/dev/cvi-tpu0) and ION allocator with standard DRM GEM
 * buffer management.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>

#include <drm/drm_accel.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_ioctl.h>

#include "sophgo_tpu_drv.h"
#include "uapi/sophgo_tpu.h"

static const struct drm_ioctl_desc sophgo_tpu_ioctls[] = {
	DRM_IOCTL_DEF_DRV(SOPHGO_TPU_CREATE_BO, sophgo_tpu_create_bo_ioctl, 0),
	DRM_IOCTL_DEF_DRV(SOPHGO_TPU_MMAP_BO, sophgo_tpu_mmap_bo_ioctl, 0),
	DRM_IOCTL_DEF_DRV(SOPHGO_TPU_SUBMIT, sophgo_tpu_submit_ioctl, 0),
	DRM_IOCTL_DEF_DRV(SOPHGO_TPU_WAIT, sophgo_tpu_wait_ioctl, 0),
	DRM_IOCTL_DEF_DRV(SOPHGO_TPU_SYNC, sophgo_tpu_sync_ioctl, 0),
};

DEFINE_DRM_ACCEL_FOPS(sophgo_tpu_fops);

static const struct drm_driver sophgo_tpu_driver = {
	.driver_features = DRIVER_COMPUTE_ACCEL | DRIVER_GEM,
	.fops = &sophgo_tpu_fops,
	.name = SOPHGO_TPU_DRIVER_NAME,
	.desc = SOPHGO_TPU_DRIVER_DESC,
	.major = SOPHGO_TPU_DRIVER_MAJOR,
	.minor = SOPHGO_TPU_DRIVER_MINOR,
	.ioctls = sophgo_tpu_ioctls,
	.num_ioctls = ARRAY_SIZE(sophgo_tpu_ioctls),
};

static int sophgo_tpu_probe(struct platform_device *pdev)
{
	struct sophgo_tpu_device *tdev;
	struct drm_device *drm;
	int ret;

	tdev = devm_drm_dev_alloc(&pdev->dev, &sophgo_tpu_driver,
				  struct sophgo_tpu_device, drm);
	if (IS_ERR(tdev))
		return PTR_ERR(tdev);

	drm = &tdev->drm;
	tdev->dev = &pdev->dev;
	platform_set_drvdata(pdev, tdev);

	/* MMIO regions */
	tdev->tdma_regs = devm_platform_ioremap_resource_byname(pdev, "tdma");
	if (IS_ERR(tdev->tdma_regs))
		return PTR_ERR(tdev->tdma_regs);

	tdev->tiu_regs = devm_platform_ioremap_resource_byname(pdev, "tiu");
	if (IS_ERR(tdev->tiu_regs))
		return PTR_ERR(tdev->tiu_regs);

	/* Clocks */
	tdev->clk_axi = devm_clk_get(&pdev->dev, "clk_tpu_axi");
	if (IS_ERR(tdev->clk_axi))
		return dev_err_probe(&pdev->dev, PTR_ERR(tdev->clk_axi),
				     "failed to get TPU AXI clock\n");

	tdev->clk_fab = devm_clk_get(&pdev->dev, "clk_tpu_fab");
	if (IS_ERR(tdev->clk_fab))
		return dev_err_probe(&pdev->dev, PTR_ERR(tdev->clk_fab),
				     "failed to get TPU fabric clock\n");

	/* Resets */
	tdev->rst_tdma = devm_reset_control_get(&pdev->dev, "res_tdma");
	if (IS_ERR(tdev->rst_tdma))
		return dev_err_probe(&pdev->dev, PTR_ERR(tdev->rst_tdma),
				     "failed to get TDMA reset\n");

	tdev->rst_tpu = devm_reset_control_get(&pdev->dev, "res_tpu");
	if (IS_ERR(tdev->rst_tpu))
		return dev_err_probe(&pdev->dev, PTR_ERR(tdev->rst_tpu),
				     "failed to get TPU reset\n");

	tdev->rst_tpusys = devm_reset_control_get(&pdev->dev, "res_tpusys");
	if (IS_ERR(tdev->rst_tpusys))
		return dev_err_probe(&pdev->dev, PTR_ERR(tdev->rst_tpusys),
				     "failed to get TPU subsystem reset\n");

	/* IRQ — TDMA is the second interrupt (index 1) in the DTS */
	tdev->tdma_irq = platform_get_irq(pdev, 1);
	if (tdev->tdma_irq < 0)
		return dev_err_probe(&pdev->dev, tdev->tdma_irq,
				     "failed to get TDMA IRQ\n");

	ret = devm_request_irq(&pdev->dev, tdev->tdma_irq, sophgo_tpu_hw_irq,
			       0, "sophgo-tpu-tdma", tdev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to request TDMA IRQ\n");

	/* Init synchronisation primitives */
	init_completion(&tdev->tdma_done);
	mutex_init(&tdev->submit_lock);
	tdev->seq_no = 0;

	/* Power on and init hardware */
	ret = sophgo_tpu_hw_init(tdev);
	if (ret)
		return ret;

	/* Register DRM/accel device — MUST be last */
	ret = drm_dev_register(drm, 0);
	if (ret) {
		sophgo_tpu_hw_fini(tdev);
		return ret;
	}

	dev_info(&pdev->dev,
		 "Sophgo TPU accel registered: TDMA@%pR TIU@%pR IRQ=%d\n",
		 platform_get_resource_byname(pdev, IORESOURCE_MEM, "tdma"),
		 platform_get_resource_byname(pdev, IORESOURCE_MEM, "tiu"),
		 tdev->tdma_irq);

	return 0;
}

static void sophgo_tpu_remove(struct platform_device *pdev)
{
	struct sophgo_tpu_device *tdev = platform_get_drvdata(pdev);

	drm_dev_unregister(&tdev->drm);
	sophgo_tpu_hw_fini(tdev);
}

static const struct of_device_id sophgo_tpu_of_match[] = {
	{ .compatible = "sophgo,sg2000-tpu" },
	{ .compatible = "cvitek,tpu" },		/* vendor compat */
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sophgo_tpu_of_match);

static struct platform_driver sophgo_tpu_platform_driver = {
	.probe = sophgo_tpu_probe,
	.remove = sophgo_tpu_remove,
	.driver = {
		.name = SOPHGO_TPU_DRIVER_NAME,
		.of_match_table = sophgo_tpu_of_match,
	},
};

module_platform_driver(sophgo_tpu_platform_driver);

MODULE_AUTHOR("meta-riscv contributors");
MODULE_DESCRIPTION(SOPHGO_TPU_DRIVER_DESC);
MODULE_LICENSE("GPL");
