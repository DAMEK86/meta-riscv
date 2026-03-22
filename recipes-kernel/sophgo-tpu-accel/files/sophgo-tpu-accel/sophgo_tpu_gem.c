// SPDX-License-Identifier: GPL-2.0
/*
 * Sophgo SG2000 TPU Accelerator - GEM buffer management
 *
 * Replaces the vendor's ION-based memory allocation with standard
 * DRM GEM objects backed by DMA coherent memory.
 */

#include <linux/dma-mapping.h>
#include <linux/slab.h>

#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_prime.h>

#include "sophgo_tpu_drv.h"
#include "uapi/sophgo_tpu.h"

static void sophgo_tpu_gem_free(struct drm_gem_object *obj)
{
	struct sophgo_tpu_bo *bo = to_sophgo_tpu_bo(obj);
	struct sophgo_tpu_device *tdev = to_sophgo_tpu(obj->dev);

	if (bo->vaddr)
		dma_free_coherent(tdev->dev, bo->size, bo->vaddr, bo->paddr);

	drm_gem_object_release(obj);
	kfree(bo);
}

static int sophgo_tpu_gem_vmap(struct drm_gem_object *obj,
			       struct iosys_map *map)
{
	struct sophgo_tpu_bo *bo = to_sophgo_tpu_bo(obj);

	if (!bo->vaddr)
		return -ENOMEM;

	iosys_map_set_vaddr(map, bo->vaddr);
	return 0;
}

static vm_fault_t sophgo_tpu_gem_vm_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct drm_gem_object *obj = vma->vm_private_data;
	struct sophgo_tpu_bo *bo = to_sophgo_tpu_bo(obj);
	unsigned long offset = vmf->address - vma->vm_start;

	if (offset >= bo->size)
		return VM_FAULT_SIGBUS;

	return vmf_insert_pfn(vma, vmf->address,
			      (bo->paddr + offset) >> PAGE_SHIFT);
}

static const struct vm_operations_struct sophgo_tpu_gem_vm_ops = {
	.fault = sophgo_tpu_gem_vm_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static int sophgo_tpu_gem_mmap_obj(struct drm_gem_object *obj,
				   struct vm_area_struct *vma)
{
	struct sophgo_tpu_bo *bo = to_sophgo_tpu_bo(obj);

	vm_flags_set(vma, VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP);
	vma->vm_page_prot = pgprot_writecombine(vm_get_page_prot(vma->vm_flags));
	vma->vm_ops = &sophgo_tpu_gem_vm_ops;

	return remap_pfn_range(vma, vma->vm_start,
			       bo->paddr >> PAGE_SHIFT,
			       vma->vm_end - vma->vm_start,
			       vma->vm_page_prot);
}

const struct drm_gem_object_funcs sophgo_tpu_gem_funcs = {
	.free = sophgo_tpu_gem_free,
	.vmap = sophgo_tpu_gem_vmap,
	.mmap = sophgo_tpu_gem_mmap_obj,
};

int sophgo_tpu_create_bo_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file)
{
	struct sophgo_tpu_device *tdev = to_sophgo_tpu(dev);
	struct drm_sophgo_tpu_create_bo *args = data;
	struct sophgo_tpu_bo *bo;
	int ret;

	if (!args->size)
		return -EINVAL;

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (!bo)
		return -ENOMEM;

	bo->size = PAGE_ALIGN(args->size);
	bo->base.funcs = &sophgo_tpu_gem_funcs;

	/* Allocate DMA-coherent memory */
	bo->vaddr = dma_alloc_coherent(tdev->dev, bo->size, &bo->paddr,
				       GFP_KERNEL | __GFP_ZERO);
	if (!bo->vaddr) {
		kfree(bo);
		return -ENOMEM;
	}

	/* Init GEM object */
	drm_gem_private_object_init(dev, &bo->base, bo->size);

	/* Create userspace handle */
	ret = drm_gem_handle_create(file, &bo->base, &args->handle);
	if (ret) {
		drm_gem_object_release(&bo->base);
		dma_free_coherent(tdev->dev, bo->size, bo->vaddr, bo->paddr);
		kfree(bo);
		return ret;
	}

	/* Create mmap offset */
	ret = drm_gem_create_mmap_offset(&bo->base);
	if (ret) {
		drm_gem_handle_delete(file, args->handle);
		drm_gem_object_put(&bo->base);
		return ret;
	}

	args->offset = drm_vma_node_offset_addr(&bo->base.vma_node);
	args->paddr = bo->paddr;
	args->size = bo->size;

	/* Drop the reference from handle_create (userspace holds it via handle) */
	drm_gem_object_put(&bo->base);

	return 0;
}

int sophgo_tpu_mmap_bo_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file)
{
	struct drm_sophgo_tpu_mmap_bo *args = data;
	struct drm_gem_object *obj;

	obj = drm_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	if (!drm_vma_node_offset_addr(&obj->vma_node)) {
		int ret = drm_gem_create_mmap_offset(obj);

		if (ret) {
			drm_gem_object_put(obj);
			return ret;
		}
	}

	args->offset = drm_vma_node_offset_addr(&obj->vma_node);
	drm_gem_object_put(obj);

	return 0;
}

int sophgo_tpu_sync_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file)
{
	struct sophgo_tpu_device *tdev = to_sophgo_tpu(dev);
	struct drm_sophgo_tpu_sync *args = data;
	struct drm_gem_object *obj;
	struct sophgo_tpu_bo *bo;
	size_t offset, size;

	obj = drm_gem_object_lookup(file, args->handle);
	if (!obj)
		return -ENOENT;

	bo = to_sophgo_tpu_bo(obj);
	offset = args->offset;
	size = args->size ? args->size : bo->size;

	if (offset + size > bo->size) {
		drm_gem_object_put(obj);
		return -EINVAL;
	}

	if (args->flags & SOPHGO_TPU_SYNC_FLUSH)
		dma_sync_single_for_device(tdev->dev, bo->paddr + offset,
					   size, DMA_TO_DEVICE);

	if (args->flags & SOPHGO_TPU_SYNC_INVALIDATE)
		dma_sync_single_for_cpu(tdev->dev, bo->paddr + offset,
					size, DMA_FROM_DEVICE);

	drm_gem_object_put(obj);
	return 0;
}
