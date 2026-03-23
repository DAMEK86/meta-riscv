// SPDX-License-Identifier: GPL-2.0
/*
 * Sophgo SG2000 TPU Accelerator - Job submission
 *
 * Handles submitting command buffers to the TPU hardware and
 * waiting for completion via the TDMA interrupt.
 */

#include <linux/dma-mapping.h>

#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>

#include "sophgo_tpu_drv.h"
#include "uapi/sophgo_tpu.h"

int sophgo_tpu_submit_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file)
{
	struct sophgo_tpu_device *tdev = to_sophgo_tpu(dev);
	struct drm_sophgo_tpu_submit *args = data;
	struct drm_gem_object *obj;
	struct sophgo_tpu_bo *bo;
	int ret;

	obj = drm_gem_object_lookup(file, args->cmdbuf_handle);
	if (!obj)
		return -ENOENT;

	bo = to_sophgo_tpu_bo(obj);

	if (args->cmdbuf_size > bo->size) {
		drm_gem_object_put(obj);
		return -EINVAL;
	}

	/* Flush command buffer to device before submission */
	dma_sync_single_for_device(tdev->dev, bo->paddr, args->cmdbuf_size,
				   DMA_TO_DEVICE);

	mutex_lock(&tdev->submit_lock);

	/* Assign sequence number */
	args->seq_no = ++tdev->seq_no;

	dev_dbg(tdev->dev, "submit: seq=%u handle=%u size=%u paddr=0x%llx\n",
		args->seq_no, args->cmdbuf_handle, args->cmdbuf_size,
		(unsigned long long)bo->paddr);

	/* Submit to TPU hardware */
	ret = sophgo_tpu_hw_run_dmabuf(tdev, bo->vaddr, bo->paddr);

	mutex_unlock(&tdev->submit_lock);

	drm_gem_object_put(obj);
	return ret;
}

int sophgo_tpu_wait_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file)
{
	struct sophgo_tpu_device *tdev = to_sophgo_tpu(dev);
	struct drm_sophgo_tpu_wait *args = data;
	unsigned long timeout_jiffies;
	long ret;

	if (args->timeout_ms)
		timeout_jiffies = msecs_to_jiffies(args->timeout_ms);
	else
		timeout_jiffies = MAX_SCHEDULE_TIMEOUT;

	ret = wait_for_completion_interruptible_timeout(&tdev->tdma_done,
							timeout_jiffies);
	if (ret == 0) {
		args->status = -ETIMEDOUT;
		return -ETIMEDOUT;
	} else if (ret < 0) {
		args->status = ret;
		return ret;
	}

	args->status = 0;
	return 0;
}
