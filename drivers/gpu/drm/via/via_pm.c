/*
 * Copyright © 2017-2020 Kevin Brace.
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


#include <linux/console.h>
#include <linux/pci.h>

#include <drm/drm_modeset_helper.h>

#include "via_drv.h"


int via_dev_pm_ops_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	struct via_drm_priv *dev_priv = to_via_drm_priv(drm_dev);
	int ret = 0;

	drm_dbg_kms(drm_dev, "Entered %s.\n", __func__);

	console_lock();

	/*
	 * Frame Buffer Size Control register (SR14) and GTI registers
	 * (SR66 through SR6F) need to be saved and restored upon standby
	 * resume or can lead to a display corruption issue. These registers
	 * are only available on VX800, VX855, and VX900 chipsets. This bug
	 * was observed on VIA Embedded EPIA-M830 mainboard.
	 */
	if ((pdev->device == PCI_DEVICE_ID_VIA_CHROME9_HC3) ||
		(pdev->device == PCI_DEVICE_ID_VIA_CHROME9_HCM) ||
		(pdev->device == PCI_DEVICE_ID_VIA_CHROME9_HD)) {
		dev_priv->saved_sr14 = vga_rseq(VGABASE, 0x14);

		dev_priv->saved_sr66 = vga_rseq(VGABASE, 0x66);
		dev_priv->saved_sr67 = vga_rseq(VGABASE, 0x67);
		dev_priv->saved_sr68 = vga_rseq(VGABASE, 0x68);
		dev_priv->saved_sr69 = vga_rseq(VGABASE, 0x69);
		dev_priv->saved_sr6a = vga_rseq(VGABASE, 0x6a);
		dev_priv->saved_sr6b = vga_rseq(VGABASE, 0x6b);
		dev_priv->saved_sr6c = vga_rseq(VGABASE, 0x6c);
		dev_priv->saved_sr6d = vga_rseq(VGABASE, 0x6d);
		dev_priv->saved_sr6e = vga_rseq(VGABASE, 0x6e);
		dev_priv->saved_sr6f = vga_rseq(VGABASE, 0x6f);
	}

	/*
	 * 3X5.3B through 3X5.3F are scratch pad registers.
	 * They are important for FP detection.
	 * Their values need to be saved because they get lost
	 * when resuming from standby.
	 */
	dev_priv->saved_cr3b = vga_rcrt(VGABASE, 0x3b);
	dev_priv->saved_cr3c = vga_rcrt(VGABASE, 0x3c);
	dev_priv->saved_cr3d = vga_rcrt(VGABASE, 0x3d);
	dev_priv->saved_cr3e = vga_rcrt(VGABASE, 0x3e);
	dev_priv->saved_cr3f = vga_rcrt(VGABASE, 0x3f);

	console_unlock();

	ret = drm_mode_config_helper_suspend(drm_dev);
	if (ret) {
		DRM_ERROR("Failed to prepare for suspend.\n");
		goto exit;
	}

	pci_save_state(pdev);
	pci_disable_device(pdev);
exit:
	drm_dbg_kms(drm_dev, "Exiting %s.\n", __func__);
	return ret;
}

int via_dev_pm_ops_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	struct via_drm_priv *dev_priv = to_via_drm_priv(drm_dev);
	void __iomem *regs = ioport_map(0x3c0, 100);
	u8 val;
	int ret = 0;

	drm_dbg_kms(drm_dev, "Entered %s.\n", __func__);

	if (pci_enable_device(pdev)) {
		DRM_ERROR("Failed to initialize a PCI "
				"after resume.\n");
		ret = -EIO;
		goto exit;
	}

	console_lock();

	val = ioread8(regs + 0x03);
	iowrite8(val | 0x1, regs + 0x03);
	val = ioread8(regs + 0x0C);
	iowrite8(val | 0x1, regs + 0x02);

	/*
	 * Unlock Extended IO Space.
	 */
	iowrite8(0x10, regs + 0x04);
	iowrite8(0x01, regs + 0x05);

	/*
	 * Unlock CRTC register protect.
	 */
	iowrite8(0x47, regs + 0x14);

	/*
	 * Enable MMIO.
	 */
	iowrite8(0x1a, regs + 0x04);
	val = ioread8(regs + 0x05);
	iowrite8(val | 0x38, regs + 0x05);

	/*
	 * Frame Buffer Size Control register (SR14) and GTI registers
	 * (SR66 through SR6F) need to be saved and restored upon standby
	 * resume or can lead to a display corruption issue. These registers
	 * are only available on VX800, VX855, and VX900 chipsets. This bug
	 * was observed on VIA Embedded EPIA-M830 mainboard.
	 */
	if ((pdev->device == PCI_DEVICE_ID_VIA_CHROME9_HC3) ||
		(pdev->device == PCI_DEVICE_ID_VIA_CHROME9_HCM) ||
		(pdev->device == PCI_DEVICE_ID_VIA_CHROME9_HD)) {
		vga_wseq(VGABASE, 0x14, dev_priv->saved_sr14);

		vga_wseq(VGABASE, 0x66, dev_priv->saved_sr66);
		vga_wseq(VGABASE, 0x67, dev_priv->saved_sr67);
		vga_wseq(VGABASE, 0x68, dev_priv->saved_sr68);
		vga_wseq(VGABASE, 0x69, dev_priv->saved_sr69);
		vga_wseq(VGABASE, 0x6a, dev_priv->saved_sr6a);
		vga_wseq(VGABASE, 0x6b, dev_priv->saved_sr6b);
		vga_wseq(VGABASE, 0x6c, dev_priv->saved_sr6c);
		vga_wseq(VGABASE, 0x6d, dev_priv->saved_sr6d);
		vga_wseq(VGABASE, 0x6e, dev_priv->saved_sr6e);
		vga_wseq(VGABASE, 0x6f, dev_priv->saved_sr6f);
	}

	/*
	 * 3X5.3B through 3X5.3F are scratch pad registers.
	 * They are important for FP detection.
	 * Their values need to be restored because they are undefined
	 * after resuming from standby.
	 */
	vga_wcrt(VGABASE, 0x3b, dev_priv->saved_cr3b);
	vga_wcrt(VGABASE, 0x3c, dev_priv->saved_cr3c);
	vga_wcrt(VGABASE, 0x3d, dev_priv->saved_cr3d);
	vga_wcrt(VGABASE, 0x3e, dev_priv->saved_cr3e);
	vga_wcrt(VGABASE, 0x3f, dev_priv->saved_cr3f);

	console_unlock();

	ret = drm_mode_config_helper_resume(drm_dev);
	if (ret) {
		DRM_ERROR("Failed to perform a modeset "
				"after resume.\n");
		goto exit;
	}

exit:
	drm_dbg_kms(drm_dev, "Exiting %s.\n", __func__);
	return ret;
}
