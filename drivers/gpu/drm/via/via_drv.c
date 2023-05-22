/*
 * Copyright © 2019-2021 Kevin Brace.
 * Copyright 2012 James Simmons. All Rights Reserved.
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

#include <drm/drm_aperture.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_generic.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_prime.h>

#include <drm/ttm/ttm_bo.h>

#include <uapi/drm/via_drm.h>

#include "via_drv.h"


/*
 * For now, this device driver will be disabled, unless the
 * user decides to enable it.
 */
int via_modeset = 0;

MODULE_PARM_DESC(modeset, "Enable DRM device driver "
				"(Default: Disabled, "
				"0 = Disabled,"
				"1 = Enabled)");
module_param_named(modeset, via_modeset, int, 0400);

static int via_driver_open(struct drm_device *dev,
					struct drm_file *file_priv)
{
	int ret = 0;

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
	return ret;
}

static void via_driver_postclose(struct drm_device *dev,
					struct drm_file *file_priv)
{
	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

static void via_driver_lastclose(struct drm_device *dev)
{
	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	drm_fb_helper_lastclose(dev);

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

static int via_driver_dumb_create(struct drm_file *file_priv,
					struct drm_device *dev,
					struct drm_mode_create_dumb *args)
{
	struct ttm_buffer_object *ttm_bo;
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	struct via_bo *bo;
	uint32_t handle, pitch;
	uint64_t size;
	int ret;

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	/*
	 * Calculate the parameters for the dumb buffer.
	 */
	pitch = args->width * ((args->bpp + 7) >> 3);
	size = pitch * args->height;

	ret = via_bo_create(dev, &dev_priv->bdev, size,
				ttm_bo_type_device, TTM_PL_VRAM, false, &bo);
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

	args->handle = handle;
	args->pitch = pitch;
	args->size = size;
exit:
	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
	return ret;
}

static int via_driver_dumb_map_offset(struct drm_file *file_priv,
						struct drm_device *dev,
						uint32_t handle,
						uint64_t *offset)
{
	struct drm_gem_object *gem;
	struct ttm_buffer_object *ttm_bo;
	struct via_bo *bo;
	int ret = 0;

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	gem = drm_gem_object_lookup(file_priv, handle);
	if (!gem) {
		ret = -ENOENT;
		goto exit;
	}

	ttm_bo = container_of(gem, struct ttm_buffer_object, base);
	bo = to_ttm_bo(ttm_bo);
	*offset = drm_vma_node_offset_addr(&ttm_bo->base.vma_node);

	drm_gem_object_put(gem);
exit:
	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
	return ret;
}

static const struct drm_ioctl_desc via_driver_ioctls[] = {
	DRM_IOCTL_DEF_DRV(VIA_ALLOCMEM, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_FREEMEM, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_AGP_INIT, drm_invalid_op, DRM_AUTH | DRM_MASTER),
	DRM_IOCTL_DEF_DRV(VIA_FB_INIT, drm_invalid_op, DRM_AUTH | DRM_MASTER),
	DRM_IOCTL_DEF_DRV(VIA_MAP_INIT, drm_invalid_op, DRM_AUTH | DRM_MASTER),
	DRM_IOCTL_DEF_DRV(VIA_DEC_FUTEX, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_DMA_INIT, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_CMDBUFFER, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_FLUSH, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_PCICMD, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_CMDBUF_SIZE, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_WAIT_IRQ, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_DMA_BLIT, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_BLIT_SYNC, drm_invalid_op, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(VIA_GEM_ALLOC, via_gem_alloc_ioctl, DRM_AUTH | DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(VIA_GEM_MMAP, via_gem_mmap_ioctl, DRM_AUTH | DRM_UNLOCKED),
};

static const struct file_operations via_driver_fops = {
	.owner		= THIS_MODULE,
	.open		= drm_open,
	.release	= drm_release,
	.unlocked_ioctl = drm_ioctl,
	.mmap		= drm_gem_mmap,
	.poll		= drm_poll,
	.llseek		= noop_llseek,
};

static struct drm_driver via_driver = {
	.open = via_driver_open,
	.postclose = via_driver_postclose,
	.lastclose = via_driver_lastclose,

	.gem_prime_mmap = drm_gem_prime_mmap,

	.dumb_create = via_driver_dumb_create,
	.dumb_map_offset = via_driver_dumb_map_offset,

	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,

	.driver_features = DRIVER_GEM |
				DRIVER_MODESET |
				DRIVER_ATOMIC,

	.ioctls = via_driver_ioctls,
	.num_ioctls = ARRAY_SIZE(via_driver_ioctls),

	.fops = &via_driver_fops,
};

static struct pci_device_id via_pci_table[] = {
	{0x1106, 0x3122, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0x1106, 0x7205, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0x1106, 0x3108, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0x1106, 0x3344, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0x1106, 0x3118, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0x1106, 0x3343, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0x1106, 0x3230, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0x1106, 0x3371, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0x1106, 0x3157, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0x1106, 0x1122, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0x1106, 0x5122, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0x1106, 0x7122, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0, 0, 0}
};

MODULE_DEVICE_TABLE(pci, via_pci_table);

static int via_pci_probe(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	struct drm_device *dev;
	struct via_drm_priv *dev_priv;
	int ret;

	dev_info(&pdev->dev, "Entered %s.\n", __func__);

	ret = drm_aperture_remove_conflicting_pci_framebuffers(pdev,
								&via_driver);
	if (ret) {
		goto exit;
	}

	ret = pci_enable_device(pdev);
	if (ret) {
		goto exit;
	}

	dev_priv = devm_drm_dev_alloc(&pdev->dev,
					&via_driver,
					struct via_drm_priv,
					dev);
	if (IS_ERR(dev_priv)) {
		ret = PTR_ERR(dev_priv);
		goto exit;
	}

	dev = &dev_priv->dev;

	pci_set_drvdata(pdev, dev);

	ret = via_drm_init(dev);
	if (ret) {
		goto error_disable_pci;
	}

	ret = drm_dev_register(dev, ent->driver_data);
	if (ret) {
		goto error_disable_pci;
	}

	drm_fbdev_generic_setup(dev, 32);
	goto exit;
error_disable_pci:
	pci_disable_device(pdev);
exit:
	dev_info(&pdev->dev, "Exiting %s.\n", __func__);
	return ret;
}

static void via_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	dev_info(&pdev->dev, "Entered %s.\n", __func__);

	via_drm_fini(dev);
	drm_dev_unregister(dev);

	dev_info(&pdev->dev, "Exiting %s.\n", __func__);
}

static const struct dev_pm_ops via_dev_pm_ops = {
	.suspend	= via_dev_pm_ops_suspend,
	.resume		= via_dev_pm_ops_resume,
};

static struct pci_driver via_pci_driver = {
	.name		= DRIVER_NAME,
	.id_table	= via_pci_table,
	.probe		= via_pci_probe,
	.remove		= via_pci_remove,
	.driver.pm	= &via_dev_pm_ops,
};

static int __init via_init(void)
{
	int ret = 0;

	if ((via_modeset == -1) &&
		(drm_firmware_drivers_only())) {
		via_modeset = 0;
	}

	if (!via_modeset) {
		ret = -EINVAL;
		goto exit;
	}

	ret = pci_register_driver(&via_pci_driver);

exit:
	return ret;
}

static void __exit via_exit(void)
{
	if (!via_modeset) {
		return;
	}

	pci_unregister_driver(&via_pci_driver);
}

module_init(via_init);
module_exit(via_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
