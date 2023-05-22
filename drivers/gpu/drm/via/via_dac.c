/*
 * Copyright © 2016-2018 Kevin Brace.
 * Copyright 2012 James Simmons. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Author(s):
 * Kevin Brace <kevinbrace@bracecomputerlab.com>
 * James Simmons <jsimmons@infradead.org>
 */

#include <linux/pci.h>

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>

#include "via_crtc_hw.h"
#include "via_drv.h"


/*
 * Enables or disables DAC (VGA) output.
 */
static void via_dac_power(struct via_drm_priv *dev_priv, bool outputState)
{
	struct drm_device *dev = &dev_priv->dev;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);


	via_dac_set_power(VGABASE, outputState);
	DRM_INFO("DAC (VGA) Power: %s\n",
			outputState ? "On" : "Off");

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

/*
 * Set DAC (VGA) sync polarity.
 */
static void via_dac_sync_polarity(struct via_drm_priv *dev_priv,
					unsigned int flags)
{
	struct drm_device *dev = &dev_priv->dev;
	u8 syncPolarity = 0x00;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	if (flags & DRM_MODE_FLAG_NHSYNC) {
		syncPolarity |= BIT(0);
	}

	if (flags & DRM_MODE_FLAG_NVSYNC) {
		syncPolarity |= BIT(1);
	}

	via_dac_set_sync_polarity(VGABASE, syncPolarity);
	DRM_INFO("DAC (VGA) Horizontal Sync Polarity: %s\n",
		(syncPolarity & BIT(0)) ? "-" : "+");
	DRM_INFO("DAC (VGA) Vertical Sync Polarity: %s\n",
		(syncPolarity & BIT(1)) ? "-" : "+");

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

/*
 * Sets DAC (VGA) display source.
 */
static void via_dac_display_source(struct via_drm_priv *dev_priv,
					int index)
{
	struct drm_device *dev = &dev_priv->dev;
	u8 displaySource = index;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	via_dac_set_display_source(VGABASE, displaySource & 0x01);
	DRM_INFO("DAC (VGA) Display Source: IGA%d\n",
			(displaySource & 0x01) + 1);

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

/*
 * Routines for controlling stuff on the DAC port
 */
static const struct drm_encoder_funcs via_dac_enc_funcs = {
	.destroy = via_encoder_destroy,
};

/*
 * Manage the power state of DAC (VGA).
 */
static void via_dac_dpms(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		via_dac_set_dpms_control(VGABASE, VIA_DAC_DPMS_ON);
		via_dac_power(dev_priv, true);
		break;
	case DRM_MODE_DPMS_STANDBY:
		via_dac_set_dpms_control(VGABASE, VIA_DAC_DPMS_STANDBY);
		via_dac_power(dev_priv, true);
		break;
	case DRM_MODE_DPMS_SUSPEND:
		via_dac_set_dpms_control(VGABASE, VIA_DAC_DPMS_SUSPEND);
		via_dac_power(dev_priv, true);
		break;
	case DRM_MODE_DPMS_OFF:
		via_dac_set_dpms_control(VGABASE, VIA_DAC_DPMS_OFF);
		via_dac_power(dev_priv, false);
		break;
	default:
		DRM_ERROR("Bad DPMS mode.");
		break;
	}

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

/* Pass our mode to the connectors and the CRTC to give them a chance to
 * adjust it according to limitations or connector properties, and also
 * a chance to reject the mode entirely. Useful for things like scaling.
 */
static bool via_dac_mode_fixup(struct drm_encoder *encoder,
				const struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	drm_mode_set_crtcinfo(adjusted_mode, 0);
	return true;
}

/*
 * Handle DAC (VGA) mode setting.
 */
static void via_dac_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	struct via_crtc *iga = container_of(encoder->crtc, struct via_crtc, base);

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	via_dac_sync_polarity(dev_priv, adjusted_mode->flags);
	via_dac_display_source(dev_priv, iga->index);

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

static void via_dac_prepare(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	if (encoder->crtc) {
		via_dac_set_dpms_control(VGABASE, VIA_DAC_DPMS_OFF);
		via_dac_power(dev_priv, false);
	}

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

static void via_dac_commit(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	if (encoder->crtc) {
		via_dac_set_dpms_control(VGABASE, VIA_DAC_DPMS_ON);
		via_dac_power(dev_priv, true);
	}

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

static void via_dac_disable(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	via_dac_set_dpms_control(VGABASE, VIA_DAC_DPMS_OFF);
	via_dac_power(dev_priv, false);

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

static const struct drm_encoder_helper_funcs via_dac_enc_helper_funcs = {
	.dpms = via_dac_dpms,
	.mode_fixup = via_dac_mode_fixup,
	.mode_set = via_dac_mode_set,
	.prepare = via_dac_prepare,
	.commit = via_dac_commit,
	.disable = via_dac_disable,
};

static enum drm_connector_status
via_dac_detect(struct drm_connector *connector, bool force)
{
	struct drm_device *dev = connector->dev;
	struct via_connector *con = container_of(connector,
					struct via_connector, base);
	enum drm_connector_status ret = connector_status_disconnected;
	struct i2c_adapter *i2c_bus;
	struct edid *edid = NULL;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	if (con->i2c_bus & VIA_I2C_BUS1) {
		i2c_bus = via_find_ddc_bus(0x26);
	} else {
		i2c_bus = NULL;
	}

	if (i2c_bus) {
		edid = drm_get_edid(&con->base, i2c_bus);
		if (edid) {
			if (!(edid->input & DRM_EDID_INPUT_DIGITAL)) {
				ret = connector_status_connected;
			}

			kfree(edid);
			goto exit;
		}
	}

	if (con->i2c_bus & VIA_I2C_BUS2) {
		i2c_bus = via_find_ddc_bus(0x31);
	} else {
		i2c_bus = NULL;
	}

	if (i2c_bus) {
		edid = drm_get_edid(&con->base, i2c_bus);
		if (edid) {
			if (!(edid->input & DRM_EDID_INPUT_DIGITAL)) {
				ret = connector_status_connected;
			}

			kfree(edid);
			goto exit;
		}
	}

exit:
	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
	return ret;
}

static const struct drm_connector_funcs via_dac_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = via_dac_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = via_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state =
			drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state =
			drm_atomic_helper_connector_destroy_state,
};

static enum drm_mode_status via_dac_mode_valid(
					struct drm_connector *connector,
					struct drm_display_mode *mode)
{
	struct drm_device *dev = connector->dev;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	int min_clock, max_clock;
	enum drm_mode_status status = MODE_OK;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	min_clock = 25000;
	switch (pdev->device) {
	/* CLE266 Chipset */
	case PCI_DEVICE_ID_VIA_CLE266_GFX:
	/* KM400(A) / KN400(A) / P4M800 Chipset */
	case PCI_DEVICE_ID_VIA_KM400_GFX:
		max_clock = 250000;
		break;
	/* K8M800(A) / K8N800(A) Chipset */
	case PCI_DEVICE_ID_VIA_K8M800_GFX:
	/* P4M800 Pro / P4M800CE / VN800 / CN700 / CN333 / CN400 Chipset */
	case PCI_DEVICE_ID_VIA_P4M800_PRO_GFX:
		max_clock = 300000;
		break;
	/* PM800 / PN800 / PM880 / PN880 Chipset */
	case PCI_DEVICE_ID_VIA_PM800_GFX:
	/* P4M890 / P4N890 Chipset */
	case PCI_DEVICE_ID_VIA_P4M890_GFX:
	/* K8M890 / K8N890 Chipset */
	case PCI_DEVICE_ID_VIA_CHROME9:
	/* P4M900 / VN896 / CN896 Chipset */
	case PCI_DEVICE_ID_VIA_CHROME9_HC:
	/* CX700(M/M2) / VX700(M/M2) Chipset */
	case PCI_DEVICE_ID_VIA_UNICHROME_PRO_II:
	/* VX800 / VX820 Chipset */
	case PCI_DEVICE_ID_VIA_CHROME9_HC3:
	/* VX855 / VX875 Chipset */
	case PCI_DEVICE_ID_VIA_CHROME9_HCM:
	/* VX900(H) Chipset */
	case PCI_DEVICE_ID_VIA_CHROME9_HD:
		max_clock = 350000;
		break;
	/* Illegal condition (should never get here) */
	default:
		max_clock = 0;
		break;
	}

	if (mode->flags & DRM_MODE_FLAG_DBLSCAN) {
		status = MODE_NO_DBLESCAN;
		goto exit;
	}

	if (mode->clock < min_clock) {
		status = MODE_CLOCK_LOW;
		goto exit;
	}

	if (mode->clock > max_clock) {
		status = MODE_CLOCK_HIGH;
		goto exit;
	}

exit:
	drm_dbg_kms(dev, "status: %u\n", status);
	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
	return status;
}

static int via_dac_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct via_connector *con = container_of(connector,
					struct via_connector, base);
	int count = 0;
	struct i2c_adapter *i2c_bus;
	struct edid *edid = NULL;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	if (con->i2c_bus & VIA_I2C_BUS1) {
		i2c_bus = via_find_ddc_bus(0x26);
	} else {
		i2c_bus = NULL;
	}

	if (i2c_bus) {
		edid = drm_get_edid(&con->base, i2c_bus);
		if (edid) {
			if (!(edid->input & DRM_EDID_INPUT_DIGITAL)) {
				drm_connector_update_edid_property(connector,
									edid);
				count = drm_add_edid_modes(connector, edid);
			}

			kfree(edid);
			goto exit;
		}
	}

	if (con->i2c_bus & VIA_I2C_BUS2) {
		i2c_bus = via_find_ddc_bus(0x31);
	} else {
		i2c_bus = NULL;
	}

	if (i2c_bus) {
		edid = drm_get_edid(&con->base, i2c_bus);
		if (edid) {
			if (!(edid->input & DRM_EDID_INPUT_DIGITAL)) {
				drm_connector_update_edid_property(connector,
									edid);
				count = drm_add_edid_modes(connector, edid);
			}

			kfree(edid);
			goto exit;
		}
	}

exit:
	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
	return count;
}

static const struct drm_connector_helper_funcs
via_dac_connector_helper_funcs = {
	.mode_valid = via_dac_mode_valid,
	.get_modes = via_dac_get_modes,
};

void via_dac_probe(struct drm_device *dev)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	u8 sr13, sr5a;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	switch (pdev->device) {
	case PCI_DEVICE_ID_VIA_UNICHROME_PRO_II:
	case PCI_DEVICE_ID_VIA_CHROME9_HC3:
	case PCI_DEVICE_ID_VIA_CHROME9_HCM:
	case PCI_DEVICE_ID_VIA_CHROME9_HD:
		sr5a = vga_rseq(VGABASE, 0x5a);
		drm_dbg_kms(dev, "SR5A: 0x%02x\n", sr5a);

		/* Setting SR5A[0] to 1.
		 * This allows the reading out the alternative
		 * pin strapping information from SR12 and SR13. */
		svga_wseq_mask(VGABASE, 0x5a, BIT(0), BIT(0));
		drm_dbg_kms(dev, "SR5A: 0x%02x\n", sr5a);

		sr13 = vga_rseq(VGABASE, 0x13);
		drm_dbg_kms(dev, "SR13: 0x%02x\n", sr13);

		if (!(sr13 & BIT(2))) {
			dev_priv->dac_presence = true;
			drm_dbg_kms(dev, "Detected the presence of VGA.\n");
		} else {
			dev_priv->dac_presence = false;
		}

		/* Restore SR5A. */
		vga_wseq(VGABASE, 0x5a, sr5a);
		break;
	default:
		dev_priv->dac_presence = true;
		drm_dbg_kms(dev, "Detected the presence of VGA.\n");
		break;
	}

	dev_priv->dac_i2c_bus = VIA_I2C_NONE;

	if (dev_priv->dac_presence) {
		dev_priv->dac_i2c_bus = VIA_I2C_BUS2 | VIA_I2C_BUS1;
	}

	dev_priv->mapped_i2c_bus |= dev_priv->dac_i2c_bus;

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

void via_dac_init(struct drm_device *dev)
{
	struct via_connector *con;
	struct via_encoder *enc;
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	enc = kzalloc(sizeof(*enc) + sizeof(*con), GFP_KERNEL);
	if (!enc) {
		DRM_ERROR("Failed to allocate connector and encoder\n");
		return;
	}
	con = &enc->cons[0];
	INIT_LIST_HEAD(&con->props);

	/* Piece together our connector */
	drm_connector_init(dev, &con->base, &via_dac_connector_funcs,
				DRM_MODE_CONNECTOR_VGA);
	drm_connector_helper_add(&con->base, &via_dac_connector_helper_funcs);
	drm_connector_register(&con->base);

	con->i2c_bus = dev_priv->dac_i2c_bus;
	con->base.doublescan_allowed = false;
	con->base.interlace_allowed = true;

	/* Setup the encoders and attach them */
	drm_encoder_init(dev, &enc->base, &via_dac_enc_funcs,
						DRM_MODE_ENCODER_DAC, NULL);
	drm_encoder_helper_add(&enc->base, &via_dac_enc_helper_funcs);

	enc->base.possible_crtcs = BIT(1) | BIT(0);
	enc->base.possible_clones = 0;
	enc->di_port = VIA_DI_PORT_NONE;

	drm_connector_attach_encoder(&con->base, &enc->base);
}
