/*
 * Copyright © 2020 Kevin Brace.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) OR COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Author(s):
 * Kevin Brace <kevinbrace@bracecomputerlab.com>
 */

#include <drm/drm_gem.h>

#include <drm/ttm/ttm_bo.h>

#include <uapi/drm/via_drm.h>

#include "via_drv.h"


int via_gem_alloc_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	struct drm_via_gem_alloc *args = data;
	struct ttm_buffer_object *ttm_bo;
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	struct via_bo *bo;
	uint32_t handle;
	int ret;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	ret = via_bo_create(dev, &dev_priv->bdev, args->size,
				ttm_bo_type_device, args->domain, false, &bo);
	if (ret) {
		goto exit;
	}

	ttm_bo = &bo->ttm_bo;

	ret = drm_gem_handle_create(file_priv, &ttm_bo->base, &handle);
	drm_gem_object_put(&ttm_bo->base);
	if (ret) {
		via_bo_destroy(bo, false);
		goto exit;
	}

	args->size	= ttm_bo->base.size;
	args->domain	= ttm_bo->resource->placement;
	args->handle	= handle;
	args->offset	= ttm_bo->resource->start << PAGE_SHIFT;
exit:
	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
	return ret;
}

int via_gem_mmap_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	struct drm_via_gem_mmap *args = data;
	struct drm_gem_object *gem;
	struct ttm_buffer_object *ttm_bo;
	int ret = 0;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	gem = drm_gem_object_lookup(file_priv, args->handle);
	if (!gem) {
		ret = -EINVAL;
		goto exit;
	}

	ttm_bo = container_of(gem, struct ttm_buffer_object, base);

	args->offset = drm_vma_node_offset_addr(&ttm_bo->base.vma_node);
exit:
	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
	return ret;
}
