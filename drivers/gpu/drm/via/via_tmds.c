/*
 * Copyright © 2016-2018 Kevin Brace.
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
 */

#include <linux/pci.h>

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>

#include "via_drv.h"


static void via_tmds_power(struct drm_device *dev, bool power_state)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	if (power_state) {
		via_lvds1_set_soft_display_period(VGABASE, true);
		via_lvds1_set_soft_data(VGABASE, true);
		via_tmds_set_power(VGABASE, true);
	} else {
		via_tmds_set_power(VGABASE, false);
		via_lvds1_set_soft_data(VGABASE, false);
		via_lvds1_set_soft_display_period(VGABASE, false);
	}

	drm_dbg_driver(dev, "DVI Power: %s\n",
			power_state ? "On" : "Off");

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

static void via_tmds_io_pad_setting(struct drm_device *dev,
					u32 di_port, bool io_pad_on)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	switch(di_port) {
	case VIA_DI_PORT_TMDS:
		via_lvds1_set_io_pad_setting(VGABASE,
				io_pad_on ? 0x03 : 0x00);
		break;
	default:
		break;
	}

	drm_dbg_kms(dev, "DVI I/O Pad: %s\n", io_pad_on ? "On": "Off");

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

/*
 * Initializes most registers related to VIA Technologies IGP
 * integrated TMDS transmitter. Synchronization polarity and
 * display output source need to be set separately.
 */
static void via_tmds_init_reg(struct drm_device *dev)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	/* Turn off hardware controlled FP power on / off circuit. */
	via_lvds_set_primary_hard_power(VGABASE, false);

	/* Use software FP power sequence control. */
	via_lvds_set_primary_power_seq_type(VGABASE, false);

	/* Turn off software controlled primary FP power rails. */
	via_lvds_set_primary_soft_vdd(VGABASE, false);
	via_lvds_set_primary_soft_vee(VGABASE, false);

	/* Turn off software controlled primary FP back light
	* control. */
	via_lvds_set_primary_soft_back_light(VGABASE, false);

	/* Turn off direct control of FP back light. */
	via_lvds_set_primary_direct_back_light_ctrl(VGABASE, false);

	/* Activate DVI + LVDS2 mode. */
	/* 3X5.D2[5:4] - Display Channel Select
	 *               00: LVDS1 + LVDS2
	 *               01: DVI + LVDS2
	 *               10: One Dual LVDS Channel (High Resolution Pannel)
	 *               11: Single Channel DVI */
	svga_wcrt_mask(VGABASE, 0xd2, 0x10, 0x30);

	/* Various DVI PLL settings should be set to default settings. */
	/* 3X5.D1[7]   - PLL2 Reference Clock Edge Select Bit
	 *               0: PLLCK lock to rising edge of reference clock
	 *               1: PLLCK lock to falling edge of reference clock
	 * 3X5.D1[6:5] - PLL2 Charge Pump Current Set Bits
	 *               00: ICH = 12.5 uA
	 *               01: ICH = 25.0 uA
	 *               10: ICH = 37.5 uA
	 *               11: ICH = 50.0 uA
	 * 3X5.D1[4:1] - Reserved
	 * 3X5.D1[0]   - PLL2 Control Voltage Measurement Enable Bit */
	svga_wcrt_mask(VGABASE, 0xd1, 0x00, 0xe1);

	/* Disable DVI test mode. */
	/* 3X5.D5[7] - PD1 Enable Selection
	 *             1: Select by power flag
	 *             0: By register
	 * 3X5.D5[5] - DVI Testing Mode Enable
	 * 3X5.D5[4] - DVI Testing Format Selection
	 *             0: Half cycle
	 *             1: LFSR mode */
	svga_wcrt_mask(VGABASE, 0xd5, 0x00, 0xb0);

	/* Disable DVI sense interrupt. */
	/* 3C5.2B[7] - DVI Sense Interrupt Enable
	 *             0: Disable
	 *             1: Enable */
	svga_wseq_mask(VGABASE, 0x2b, 0x00, 0x80);

	/* Clear DVI sense interrupt status. */
	/* 3C5.2B[6] - DVI Sense Interrupt Status
	 *             (This bit has a RW1C attribute.) */
	svga_wseq_mask(VGABASE, 0x2b, 0x40, 0x40);

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

/*
 * Set TMDS (DVI) sync polarity.
 */
static void via_tmds_sync_polarity(struct drm_device *dev,
					unsigned int flags)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	u8 syncPolarity = 0x00;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	if (flags & DRM_MODE_FLAG_NHSYNC) {
		syncPolarity |= BIT(0);
	}

	if (flags & DRM_MODE_FLAG_NVSYNC) {
		syncPolarity |= BIT(1);
	}

	via_tmds_set_sync_polarity(VGABASE, syncPolarity);
	drm_dbg_driver(dev, "TMDS (DVI) Horizontal Sync Polarity: %s\n",
		(syncPolarity & BIT(0)) ? "-" : "+");
	drm_dbg_driver(dev, "TMDS (DVI) Vertical Sync Polarity: %s\n",
		(syncPolarity & BIT(1)) ? "-" : "+");

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

/*
 * Sets TMDS (DVI) display source.
 */
static void via_tmds_display_source(struct drm_device *dev, int index)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	u8 displaySource = index;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	via_tmds_set_display_source(VGABASE, displaySource & 0x01);
	drm_dbg_driver(dev, "TMDS (DVI) Display Source: IGA%d\n",
			(displaySource & 0x01) + 1);

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

/*
 * Routines for controlling stuff on the TMDS port
 */
static const struct drm_encoder_funcs via_tmds_enc_funcs = {
	.destroy = via_encoder_destroy,
};

static void via_tmds_dpms(struct drm_encoder *encoder, int mode)
{
	struct via_encoder *enc = container_of(encoder,
					struct via_encoder, base);
	struct drm_device *dev = encoder->dev;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		via_tmds_power(dev, true);
		via_tmds_io_pad_setting(dev, enc->di_port, true);
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		via_tmds_power(dev, false);
		via_tmds_io_pad_setting(dev, enc->di_port, false);
		break;
	default:
		drm_err(dev, "Bad DPMS mode.");
		break;
	}

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

/* Pass our mode to the connectors and the CRTC to give them a chance to
 * adjust it according to limitations or connector properties, and also
 * a chance to reject the mode entirely. Usefule for things like scaling.
 */
static bool via_tmds_mode_fixup(struct drm_encoder *encoder,
				const struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	drm_mode_set_crtcinfo(adjusted_mode, 0);
	return true;
}

static void via_tmds_prepare(struct drm_encoder *encoder)
{
	struct via_encoder *enc = container_of(encoder,
					struct via_encoder, base);
	struct drm_device *dev = encoder->dev;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	via_tmds_power(dev, false);
	via_tmds_io_pad_setting(dev, enc->di_port, false);

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

static void via_tmds_commit(struct drm_encoder *encoder)
{
	struct via_encoder *enc = container_of(encoder,
					struct via_encoder, base);
	struct drm_device *dev = encoder->dev;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	via_tmds_power(dev, true);
	via_tmds_io_pad_setting(dev, enc->di_port, true);

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

/*
 * Handle CX700 / VX700 and VX800 integrated TMDS (DVI) mode setting.
 */
static void via_tmds_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct via_crtc *iga = container_of(encoder->crtc,
						struct via_crtc, base);

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	via_tmds_init_reg(dev);
	via_tmds_sync_polarity(dev, adjusted_mode->flags);
	via_tmds_display_source(dev, iga->index);

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

static void via_tmds_disable(struct drm_encoder *encoder)
{
	struct via_encoder *enc = container_of(encoder,
					struct via_encoder, base);
	struct drm_device *dev = encoder->dev;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	via_tmds_power(dev, false);
	via_tmds_io_pad_setting(dev, enc->di_port, false);

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

static const struct drm_encoder_helper_funcs
via_tmds_enc_helper_funcs = {
	.dpms = via_tmds_dpms,
	.mode_fixup = via_tmds_mode_fixup,
	.prepare = via_tmds_prepare,
	.commit = via_tmds_commit,
	.mode_set = via_tmds_mode_set,
	.disable = via_tmds_disable,
};

static enum drm_connector_status via_tmds_detect(
					struct drm_connector *connector,
					bool force)
{
	struct drm_device *dev = connector->dev;

	struct via_connector *con = container_of(connector, struct via_connector, base);
	enum drm_connector_status ret = connector_status_disconnected;
	struct i2c_adapter *i2c_bus;
	struct edid *edid = NULL;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	if (con->i2c_bus & VIA_I2C_BUS2) {
		i2c_bus = via_find_ddc_bus(0x31);
	} else if (con->i2c_bus & VIA_I2C_BUS3) {
		i2c_bus = via_find_ddc_bus(0x2c);
	} else {
		i2c_bus = NULL;
	}

	if (i2c_bus) {
		edid = drm_get_edid(&con->base, i2c_bus);
		if (edid) {
			if (edid->input & DRM_EDID_INPUT_DIGITAL) {
				drm_connector_update_edid_property(connector, edid);
				ret = connector_status_connected;
			}

			kfree(edid);
		}
	}

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
	return ret;
}

static const struct drm_connector_funcs via_dvi_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = via_tmds_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = via_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state =
			drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state =
			drm_atomic_helper_connector_destroy_state,
};

static enum drm_mode_status via_tmds_mode_valid(
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
	/* CX700(M/M2) / VX700(M/M2) Chipset */
	case PCI_DEVICE_ID_VIA_UNICHROME_PRO_II:
	/* VX800 / VX820 Chipset */
	case PCI_DEVICE_ID_VIA_CHROME9_HC3:
		max_clock = 165000;
		break;
	/* Illegal condition (should never get here) */
	default:
		max_clock = 0;
		break;
	}

	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		status = MODE_NO_INTERLACE;
		goto exit;
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

static int via_tmds_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct via_connector *con = container_of(connector, struct via_connector, base);
	struct i2c_adapter *i2c_bus;
	struct edid *edid = NULL;
	int count = 0;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	if (con->i2c_bus & VIA_I2C_BUS2) {
		i2c_bus = via_find_ddc_bus(0x31);
	} else if (con->i2c_bus & VIA_I2C_BUS3) {
		i2c_bus = via_find_ddc_bus(0x2c);
	} else {
		i2c_bus = NULL;
	}

	if (i2c_bus) {
		edid = drm_get_edid(&con->base, i2c_bus);
		if (edid->input & DRM_EDID_INPUT_DIGITAL) {
			drm_connector_update_edid_property(connector,
								edid);
			count = drm_add_edid_modes(connector, edid);
			drm_dbg_kms(dev, "DVI EDID information was obtained.\n");
		}

		kfree(edid);
	}

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
	return count;
}

static const struct drm_connector_helper_funcs
via_dvi_connector_helper_funcs = {
	.mode_valid = via_tmds_mode_valid,
	.get_modes = via_tmds_get_modes,
};

/*
 * Probe (pre-initialization detection) of integrated TMDS transmitters.
 */
void via_tmds_probe(struct drm_device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	u8 sr13, sr5a;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	/* Detect the presence of integrated TMDS transmitter. */
	switch (pdev->device) {
	case PCI_DEVICE_ID_VIA_UNICHROME_PRO_II:
	case PCI_DEVICE_ID_VIA_CHROME9_HC3:
		sr5a = vga_rseq(VGABASE, 0x5a);

		/* Setting SR5A[0] to 1.
		 * This allows the reading out the alternative
		 * pin strapping information from SR12 and SR13. */
		svga_wseq_mask(VGABASE, 0x5a, BIT(0), BIT(0));

		sr13 = vga_rseq(VGABASE, 0x13);
		drm_dbg_kms(dev, "sr13: 0x%02x\n", sr13);

		vga_wseq(VGABASE, 0x5a, sr5a);

		/* 3C5.13[7:6] - Integrated LVDS / DVI Mode Select
		 *               (DVP1D15-14 pin strapping)
		 *               00: LVDS1 + LVDS2
		 *               01: DVI + LVDS2
		 *               10: Dual LVDS Channel (High Resolution Panel)
		 *               11: One DVI only (decrease the clock jitter) */
		/* Check for DVI presence using pin strappings.
		 * VIA Technologies NanoBook reference design based products
		 * have their pin strappings set to a wrong setting to communicate
		 * the presence of DVI, so it requires special handling here. */
		if (dev_priv->is_via_nanobook) {
			dev_priv->int_tmds_presence = true;
			dev_priv->int_tmds_di_port = VIA_DI_PORT_TMDS;
			dev_priv->int_tmds_i2c_bus = VIA_I2C_BUS2;
			dev_priv->mapped_i2c_bus |= VIA_I2C_BUS2;
			drm_dbg_kms(dev, "Integrated TMDS (DVI) "
					"transmitter detected.\n");
		} else if (((!(sr13 & BIT(7))) && (sr13 & BIT(6))) ||
				((sr13 & BIT(7)) && (sr13 & BIT(6)))) {
			dev_priv->int_tmds_presence = true;
			dev_priv->int_tmds_di_port = VIA_DI_PORT_TMDS;
			dev_priv->int_tmds_i2c_bus = VIA_I2C_BUS2;
			dev_priv->mapped_i2c_bus |= VIA_I2C_BUS2;
			drm_dbg_kms(dev, "Integrated TMDS (DVI) "
					"transmitter detected via pin "
					"strapping.\n");
		} else {
			dev_priv->int_tmds_presence = false;
			dev_priv->int_tmds_di_port = VIA_DI_PORT_NONE;
			dev_priv->int_tmds_i2c_bus = VIA_I2C_NONE;
		}

		break;
	default:
		dev_priv->int_tmds_presence = false;
		dev_priv->int_tmds_di_port = VIA_DI_PORT_NONE;
		dev_priv->int_tmds_i2c_bus = VIA_I2C_NONE;
		break;
	}

	drm_dbg_kms(dev, "int_tmds_presence: %x\n",
			dev_priv->int_tmds_presence);
	drm_dbg_kms(dev, "int_tmds_di_port: 0x%08x\n",
			dev_priv->int_tmds_di_port);
	drm_dbg_kms(dev, "int_tmds_i2c_bus: 0x%08x\n",
			dev_priv->int_tmds_i2c_bus);
	drm_dbg_kms(dev, "mapped_i2c_bus: 0x%08x\n",
			dev_priv->mapped_i2c_bus);

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

void via_tmds_init(struct drm_device *dev)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	struct via_connector *con;
	struct via_encoder *enc;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	if (!dev_priv->int_tmds_presence) {
		goto exit;
	}

	enc = kzalloc(sizeof(*enc) + sizeof(*con), GFP_KERNEL);
	if (!enc) {
		drm_err(dev, "Failed to allocate connector "
				"and encoder.\n");
		goto exit;
	}

	/* Setup the encoders and attach them */
	drm_encoder_init(dev, &enc->base, &via_tmds_enc_funcs,
				DRM_MODE_ENCODER_TMDS, NULL);
	drm_encoder_helper_add(&enc->base, &via_tmds_enc_helper_funcs);

	enc->base.possible_crtcs = BIT(1) | BIT(0);
	enc->base.possible_clones = 0;

	enc->di_port = dev_priv->int_tmds_di_port;

	/* Increment the number of DVI connectors. */
	dev_priv->number_dvi++;


	con = &enc->cons[0];
	drm_connector_init(dev, &con->base, &via_dvi_connector_funcs,
				DRM_MODE_CONNECTOR_DVID);
	drm_connector_helper_add(&con->base, &via_dvi_connector_helper_funcs);
	drm_connector_register(&con->base);

	con->i2c_bus = dev_priv->int_tmds_i2c_bus;
	con->base.doublescan_allowed = false;
	con->base.interlace_allowed = true;
	INIT_LIST_HEAD(&con->props);

	drm_connector_attach_encoder(&con->base, &enc->base);
exit:
	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

/*
 * Probe (pre-initialization detection) of external DVI transmitters.
 */
void via_ext_dvi_probe(struct drm_device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	struct i2c_adapter *i2c_bus;
	u8 sr12, sr13;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	dev_priv->ext_tmds_presence = false;
	dev_priv->ext_tmds_i2c_bus = VIA_I2C_NONE;
	dev_priv->ext_tmds_transmitter = VIA_TMDS_NONE;

	if ((!dev_priv->ext_tmds_presence) &&
		(!(dev_priv->mapped_i2c_bus & VIA_I2C_BUS2))) {
		i2c_bus = via_find_ddc_bus(0x31);
		if (via_vt1632_probe(dev, i2c_bus)) {
			dev_priv->ext_tmds_presence = true;
			dev_priv->ext_tmds_i2c_bus = VIA_I2C_BUS2;
			dev_priv->ext_tmds_transmitter = VIA_TMDS_VT1632;
			dev_priv->mapped_i2c_bus |= VIA_I2C_BUS2;
		} else if (via_sii164_probe(dev, i2c_bus)) {
			dev_priv->ext_tmds_presence = true;
			dev_priv->ext_tmds_i2c_bus = VIA_I2C_BUS2;
			dev_priv->ext_tmds_transmitter = VIA_TMDS_SII164;
			dev_priv->mapped_i2c_bus |= VIA_I2C_BUS2;
		}
	}

	if ((!(dev_priv->ext_tmds_presence)) &&
		(!(dev_priv->mapped_i2c_bus & VIA_I2C_BUS4))) {
		i2c_bus = via_find_ddc_bus(0x2c);
		if (via_vt1632_probe(dev, i2c_bus)) {
			dev_priv->ext_tmds_presence = true;
			dev_priv->ext_tmds_i2c_bus = VIA_I2C_BUS4;
			dev_priv->ext_tmds_transmitter = VIA_TMDS_VT1632;
			dev_priv->mapped_i2c_bus |= VIA_I2C_BUS4;
		} else if (via_sii164_probe(dev, i2c_bus)) {
			dev_priv->ext_tmds_presence = true;
			dev_priv->ext_tmds_i2c_bus = VIA_I2C_BUS4;
			dev_priv->ext_tmds_transmitter = VIA_TMDS_SII164;
			dev_priv->mapped_i2c_bus |= VIA_I2C_BUS4;
		}
	}

	sr12 = vga_rseq(VGABASE, 0x12);
	sr13 = vga_rseq(VGABASE, 0x13);
	drm_dbg_kms(dev, "SR12: 0x%02x\n", sr12);
	drm_dbg_kms(dev, "SR13: 0x%02x\n", sr13);

	if (dev_priv->ext_tmds_presence) {
		switch (pdev->device) {
		case PCI_DEVICE_ID_VIA_CLE266_GFX:
			/* 3C5.12[5] - FPD18 pin strapping (DIP0)
			 *             0: DVI
			 *             1: TV */
			if (!(sr12 & BIT(5))) {
				dev_priv->ext_tmds_di_port = VIA_DI_PORT_DIP0;

			/* 3C5.12[4] - FPD17 pin strapping (DIP1)
			 *             0: DVI / Capture
			 *             1: Panel */
			} else if (!(sr12 & BIT(4))) {
				dev_priv->ext_tmds_di_port = VIA_DI_PORT_DIP1;
			} else {
				dev_priv->ext_tmds_di_port = VIA_DI_PORT_NONE;
			}

			break;
		case PCI_DEVICE_ID_VIA_KM400_GFX:
		case PCI_DEVICE_ID_VIA_K8M800_GFX:
		case PCI_DEVICE_ID_VIA_P4M800_PRO_GFX:
		case PCI_DEVICE_ID_VIA_PM800_GFX:
			/*
			 * For DVP0 to be configured to not be used for
			 * a TV encoder, DVP0D[6] (SR12[6]) needs to be
			 * strapped low (0).  In addition, DVP0D[5]
			 * (SR12[5]) also needs to be strapped low (0)
			 * for DVP0 to be configured for DVI
			 * transmitter use.
			 */
			if (!(sr12 & BIT(6)) && (!(sr12 & BIT(5)))) {
				dev_priv->ext_tmds_di_port = VIA_DI_PORT_DVP0;
			} else {
				dev_priv->ext_tmds_di_port = VIA_DI_PORT_NONE;
			}

			break;
		case PCI_DEVICE_ID_VIA_P4M890_GFX:
		case PCI_DEVICE_ID_VIA_CHROME9:
		case PCI_DEVICE_ID_VIA_CHROME9_HC:
			/* Assume DVP2 as DVP0. Hence, VIA_DI_PORT_DVP0
			 * is used. */
			/* 3C5.12[6] - DVP2D6 pin strapping
			 *             0: Disable DVP2 (Digital Video Port 2)
			 *             1: Enable DVP2 (Digital Video Port 2)
			 * 3C5.12[5] - DVP2D5 pin strapping
			 *             0: TMDS transmitter (DVI)
			 *             1: TV encoder */
			if ((sr12 & BIT(6)) && (!(sr12 & BIT(5)))) {
				dev_priv->ext_tmds_di_port = VIA_DI_PORT_DVP0;
			} else {
				dev_priv->ext_tmds_di_port = VIA_DI_PORT_NONE;
			}

			break;
		case PCI_DEVICE_ID_VIA_UNICHROME_PRO_II:
		case PCI_DEVICE_ID_VIA_CHROME9_HC3:
		case PCI_DEVICE_ID_VIA_CHROME9_HCM:
		case PCI_DEVICE_ID_VIA_CHROME9_HD:
			dev_priv->ext_tmds_di_port = VIA_DI_PORT_DVP1;
			break;
		default:
			dev_priv->ext_tmds_di_port = VIA_DI_PORT_NONE;
			break;
		}
	}

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

void via_ext_dvi_init(struct drm_device *dev)
{
	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	via_vt1632_init(dev);
	via_sii164_init(dev);

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}
