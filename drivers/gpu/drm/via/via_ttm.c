/*
 * Copyright © 2018-2019 Kevin Brace.
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
/*
 * via_ttm.c
 *
 * TTM code as part of the TTM memory allocator.
 * Currently a basic implementation with no DMA support.
 *
 */

#include <linux/pci.h>

#include <drm/ttm/ttm_bo.h>
#include <drm/ttm/ttm_tt.h>

#include "via_drv.h"


static void via_bo_move_notify(struct ttm_buffer_object *bo, bool evict,
				struct ttm_resource *new_mem)
{
	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
	return;
}

static struct ttm_tt *via_ttm_tt_create(struct ttm_buffer_object *bo,
					uint32_t page_flags)
{
	struct ttm_tt *tt;
	int ret;

	tt = kzalloc(sizeof(*tt), GFP_KERNEL);
	if (!tt)
		return NULL;

	ret = ttm_tt_init(tt, bo, page_flags, ttm_cached, 0);
	if (ret < 0)
		goto err_ttm_tt_init;

	return tt;

err_ttm_tt_init:
	kfree(tt);
	return NULL;
}

static void via_ttm_tt_destroy(struct ttm_device *bdev, struct ttm_tt *tt)
{
	ttm_tt_fini(tt);
	kfree(tt);
}

static void via_bo_evict_flags(struct ttm_buffer_object *bo,
				struct ttm_placement *placement)
{
	struct via_bo *driver_bo = to_ttm_bo(bo);

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	if (bo->destroy == &via_ttm_bo_destroy) {
		goto exit;
	}

	switch (bo->resource->mem_type) {
	case TTM_PL_VRAM:
		via_ttm_domain_to_placement(driver_bo, TTM_PL_VRAM);
		break;
	default:
		via_ttm_domain_to_placement(driver_bo, TTM_PL_SYSTEM);
		break;
	}

	*placement = driver_bo->placement;
exit:
	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

static int via_bo_move(struct ttm_buffer_object *bo, bool evict,
			struct ttm_operation_ctx *ctx,
			struct ttm_resource *new_mem,
			struct ttm_place *hop)
{
	int ret;

	if (!bo->resource) {
		if (new_mem->mem_type != TTM_PL_SYSTEM) {
			hop->mem_type = TTM_PL_SYSTEM;
			hop->flags = TTM_PL_FLAG_TEMPORARY;
			return -EMULTIHOP;
		}

		ttm_bo_move_null(bo, new_mem);
		return 0;
	}

	via_bo_move_notify(bo, evict, new_mem);
	ret = ttm_bo_move_memcpy(bo, ctx, new_mem);
	if (ret) {
		swap(*new_mem, *bo->resource);
		via_bo_move_notify(bo, false, new_mem);
		swap(*new_mem, *bo->resource);
	}

	return ret;
}

static void via_bo_delete_mem_notify(struct ttm_buffer_object *bo)
{
	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	via_bo_move_notify(bo, false, NULL);

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
	return;
}

static int via_bo_io_mem_reserve(struct ttm_device *bdev,
					struct ttm_resource *mem)
{
	struct via_drm_priv *dev_priv = container_of(bdev,
						struct via_drm_priv, bdev);
	int ret = 0;

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	switch (mem->mem_type) {
	case TTM_PL_SYSTEM:
		break;
	case TTM_PL_VRAM:
		mem->bus.offset = dev_priv->vram_start +
					(mem->start << PAGE_SHIFT);
		mem->bus.is_iomem = true;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
	return ret;
}

struct ttm_device_funcs via_bo_driver = {
	.ttm_tt_create = via_ttm_tt_create,
	.ttm_tt_destroy = via_ttm_tt_destroy,
	.eviction_valuable = ttm_bo_eviction_valuable,
	.evict_flags = via_bo_evict_flags,
	.move = via_bo_move,
	.delete_mem_notify = via_bo_delete_mem_notify,
	.io_mem_reserve = via_bo_io_mem_reserve,
};
