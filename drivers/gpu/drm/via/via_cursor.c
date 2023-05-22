/*
 * Copyright © 2019-2020 Kevin Brace.
 * Copyright 2012 James Simmons. All Rights Reserved.
 * Copyright 1998-2009 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2009 S3 Graphics, Inc. All Rights Reserved.
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
 * James Simmons <jsimmons@infradead.org>
 */

#include <linux/pci.h>
#include <linux/pci_ids.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>
#include <drm/drm_mode.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_plane.h>

#include <drm/ttm/ttm_bo.h>

#include "via_drv.h"


static void via_hide_cursor(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct via_crtc *iga = container_of(crtc,
					struct via_crtc, base);
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	uint32_t temp;

	switch (pdev->device) {
	case PCI_DEVICE_ID_VIA_UNICHROME_PRO_II:
	case PCI_DEVICE_ID_VIA_P4M890_GFX:
	case PCI_DEVICE_ID_VIA_CHROME9_HC:
	case PCI_DEVICE_ID_VIA_CHROME9_HC3:
	case PCI_DEVICE_ID_VIA_CHROME9_HCM:
	case PCI_DEVICE_ID_VIA_CHROME9_HD:
		if (iga->index) {
			temp = VIA_READ(HI_CONTROL);
			VIA_WRITE(HI_CONTROL, temp & 0xFFFFFFFA);
		} else {
			temp = VIA_READ(PRIM_HI_CTRL);
			VIA_WRITE(PRIM_HI_CTRL, temp & 0xFFFFFFFA);
		}

		break;
	default:
		temp = VIA_READ(HI_CONTROL);
		VIA_WRITE(HI_CONTROL, temp & 0xFFFFFFFA);
		break;
	}
}

static void via_show_cursor(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct via_crtc *iga = container_of(crtc,
					struct via_crtc, base);
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	switch (pdev->device) {
	case PCI_DEVICE_ID_VIA_UNICHROME_PRO_II:
	case PCI_DEVICE_ID_VIA_P4M890_GFX:
	case PCI_DEVICE_ID_VIA_CHROME9_HC:
	case PCI_DEVICE_ID_VIA_CHROME9_HC3:
	case PCI_DEVICE_ID_VIA_CHROME9_HCM:
	case PCI_DEVICE_ID_VIA_CHROME9_HD:
		/*
		 * Program Hardware Icon (HI) FIFO, foreground color,
		 * and background color.
		 */
		if (iga->index) {
			VIA_WRITE(HI_TRANSPARENT_COLOR, 0x00000000);
			VIA_WRITE(HI_INVTCOLOR, 0x00FFFFFF);
			VIA_WRITE(ALPHA_V3_PREFIFO_CONTROL,
							0x000E0000);
			VIA_WRITE(ALPHA_V3_FIFO_CONTROL, 0x0E0F0000);
		} else {
			VIA_WRITE(PRIM_HI_TRANSCOLOR, 0x00000000);
			VIA_WRITE(PRIM_HI_INVTCOLOR, 0x00FFFFFF);
			VIA_WRITE(V327_HI_INVTCOLOR, 0x00FFFFFF);
			VIA_WRITE(PRIM_HI_FIFO, 0x0D000D0F);
		}

		break;
	default:
		/*
		 * Program Hardware Icon (HI) FIFO, foreground color,
		 * and background color.
		 */
		VIA_WRITE(HI_TRANSPARENT_COLOR, 0x00000000);
		VIA_WRITE(HI_INVTCOLOR, 0x00FFFFFF);
		VIA_WRITE(ALPHA_V3_PREFIFO_CONTROL, 0x000E0000);
		VIA_WRITE(ALPHA_V3_FIFO_CONTROL, 0xE0F0000);
		break;
	}

	switch (pdev->device) {
	case PCI_DEVICE_ID_VIA_UNICHROME_PRO_II:
	case PCI_DEVICE_ID_VIA_P4M890_GFX:
	case PCI_DEVICE_ID_VIA_CHROME9_HC:
	case PCI_DEVICE_ID_VIA_CHROME9_HC3:
	case PCI_DEVICE_ID_VIA_CHROME9_HCM:
	case PCI_DEVICE_ID_VIA_CHROME9_HD:
		/*
		 * Turn on Hardware Icon (HI).
		 */
		if (iga->index) {
			VIA_WRITE(HI_CONTROL, 0xB6000005);
		} else {
			VIA_WRITE(PRIM_HI_CTRL, 0x36000005);
		}

		break;
	default:
		/*
		 * Turn on Hardware Icon (HI).
		 */
		if (iga->index) {
			VIA_WRITE(HI_CONTROL, 0xB6000005);
		} else {
			VIA_WRITE(HI_CONTROL, 0x36000005);
		}

		break;
	}
}

static void via_cursor_address(struct drm_crtc *crtc,
				struct ttm_buffer_object *ttm_bo)
{
	struct drm_device *dev = crtc->dev;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct via_crtc *iga = container_of(crtc,
					struct via_crtc, base);
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	switch (pdev->device) {
	case PCI_DEVICE_ID_VIA_UNICHROME_PRO_II:
	case PCI_DEVICE_ID_VIA_P4M890_GFX:
	case PCI_DEVICE_ID_VIA_CHROME9_HC:
	case PCI_DEVICE_ID_VIA_CHROME9_HC3:
	case PCI_DEVICE_ID_VIA_CHROME9_HCM:
	case PCI_DEVICE_ID_VIA_CHROME9_HD:
		/*
		 * Program Hardware Icon (HI) offset.
		 */
		if (iga->index) {
			VIA_WRITE(HI_FBOFFSET,
				ttm_bo->resource->start << PAGE_SHIFT);
		} else {
			VIA_WRITE(PRIM_HI_FBOFFSET,
				ttm_bo->resource->start << PAGE_SHIFT);
		}
		break;
	default:
		/*
		 * Program Hardware Icon (HI) offset.
		 */
		VIA_WRITE(HI_FBOFFSET, ttm_bo->resource->start << PAGE_SHIFT);
		break;
	}

	return;
}

static void via_set_hi_location(struct drm_crtc *crtc, int crtc_x, int crtc_y)
{
	struct drm_device *dev = crtc->dev;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct via_crtc *iga = container_of(crtc,
					struct via_crtc, base);
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	uint32_t location_x = 0, location_y = 0;
	uint32_t offset_x = 0, offset_y = 0;

	if (crtc_x < 0) {
		offset_x = -crtc_x;
	} else {
		location_x = crtc_x;
	}

	if (crtc_y < 0) {
		offset_y = -crtc_y;
	} else {
		location_y = crtc_y;
	}

	switch (pdev->device) {
	case PCI_DEVICE_ID_VIA_UNICHROME_PRO_II:
	case PCI_DEVICE_ID_VIA_P4M890_GFX:
	case PCI_DEVICE_ID_VIA_CHROME9_HC:
	case PCI_DEVICE_ID_VIA_CHROME9_HC3:
	case PCI_DEVICE_ID_VIA_CHROME9_HCM:
	case PCI_DEVICE_ID_VIA_CHROME9_HD:
		if (iga->index) {
			VIA_WRITE(HI_POSSTART,
				(((location_x & 0x07ff) << 16) |
				(location_y & 0x07ff)));
			VIA_WRITE(HI_CENTEROFFSET,
				(((offset_x & 0x07ff) << 16) |
				(offset_y & 0x07ff)));
		} else {
			VIA_WRITE(PRIM_HI_POSSTART,
				(((location_x & 0x07ff) << 16) |
				(location_y & 0x07ff)));
			VIA_WRITE(PRIM_HI_CENTEROFFSET,
				(((offset_x & 0x07ff) << 16) |
				(offset_y & 0x07ff)));
		}

		break;
	default:
		VIA_WRITE(HI_POSSTART,
				(((location_x & 0x07ff) << 16) |
				(location_y & 0x07ff)));
		VIA_WRITE(HI_CENTEROFFSET,
				(((offset_x & 0x07ff) << 16) |
				(offset_y & 0x07ff)));
		break;
	}
}

static int via_cursor_prepare_fb(struct drm_plane *plane,
					struct drm_plane_state *new_state)
{
	struct drm_gem_object *gem;
	struct ttm_buffer_object *ttm_bo;
	struct via_bo *bo;
	int ret = 0;

	if (!new_state->fb) {
		goto exit;
	}

	gem = new_state->fb->obj[0];
	ttm_bo = container_of(gem, struct ttm_buffer_object, base);
	bo = to_ttm_bo(ttm_bo);

	ret = ttm_bo_reserve(ttm_bo, true, false, NULL);
	if (ret) {
		goto exit;
	}

	ret = via_bo_pin(bo, TTM_PL_VRAM);
	ttm_bo_unreserve(ttm_bo);
	ret = ttm_bo_kmap(ttm_bo, 0, PFN_UP(ttm_bo->resource->size), &bo->kmap);
	if (ret) {
		goto exit;
	}

exit:
	return ret;
}

static void via_cursor_cleanup_fb(struct drm_plane *plane,
					struct drm_plane_state *old_state)
{
	struct drm_gem_object *gem;
	struct ttm_buffer_object *ttm_bo;
	struct via_bo *bo;
	int ret;

	if (!old_state->fb) {
		goto exit;
	}

	gem = old_state->fb->obj[0];
	ttm_bo = container_of(gem, struct ttm_buffer_object, base);
	bo = to_ttm_bo(ttm_bo);

	ttm_bo_kunmap(&bo->kmap);
	ret = ttm_bo_reserve(ttm_bo, true, false, NULL);
	if (ret) {
		goto exit;
	}

	via_bo_unpin(bo);
	ttm_bo_unreserve(ttm_bo);

exit:
	return;
}

static int via_cursor_atomic_check(struct drm_plane *plane,
					struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state =
			drm_atomic_get_new_plane_state(state, plane);
	struct drm_crtc_state *new_crtc_state;
	struct drm_device *dev = plane->dev;
	struct drm_framebuffer *fb = new_plane_state->fb;
	int ret = 0;

	if ((!new_plane_state->crtc) || (!new_plane_state->visible)) {
		goto exit;
	}

	if (fb->width != fb->height) {
		drm_err(dev, "Hardware cursor is expected to have "
				"square dimensions.\n");
		ret = -EINVAL;
		goto exit;
	}

	new_crtc_state = drm_atomic_get_new_crtc_state(state,
						new_plane_state->crtc);
	ret = drm_atomic_helper_check_plane_state(
					new_plane_state,
					new_crtc_state,
					DRM_PLANE_NO_SCALING,
					DRM_PLANE_NO_SCALING,
					true, true);
exit:
	return ret;
}

static void via_cursor_atomic_update(struct drm_plane *plane,
					struct drm_atomic_state *state)
{
	struct drm_plane_state *new_state =
			drm_atomic_get_new_plane_state(state, plane);
	struct drm_plane_state *old_state =
			drm_atomic_get_old_plane_state(state, plane);
	struct drm_crtc *crtc = new_state->crtc;
	struct drm_gem_object *gem;
	struct ttm_buffer_object *ttm_bo;

	if (new_state->fb != old_state->fb) {
		gem = new_state->fb->obj[0];
		ttm_bo = container_of(gem, struct ttm_buffer_object, base);
		via_cursor_address(crtc, ttm_bo);
	}

	via_set_hi_location(crtc, new_state->crtc_x, new_state->crtc_y);
	via_show_cursor(crtc);
}

void via_cursor_atomic_disable(struct drm_plane *plane,
					struct drm_atomic_state *state)
{
	struct drm_plane_state *new_state =
			drm_atomic_get_new_plane_state(state, plane);
	struct drm_crtc *crtc = new_state->crtc;

	if (crtc) {
		via_hide_cursor(crtc);
	}
}

const struct drm_plane_helper_funcs via_cursor_drm_plane_helper_funcs = {
	.prepare_fb	= via_cursor_prepare_fb,
	.cleanup_fb	= via_cursor_cleanup_fb,
	.atomic_check	= via_cursor_atomic_check,
	.atomic_update	= via_cursor_atomic_update,
	.atomic_disable	= via_cursor_atomic_disable,
};

const struct drm_plane_funcs via_cursor_drm_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

const uint32_t via_cursor_formats[] = {
	DRM_FORMAT_XRGB8888,
};

const unsigned int via_cursor_formats_size =
				ARRAY_SIZE(via_cursor_formats);
