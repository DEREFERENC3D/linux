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


#define VIA_VT1632_VEN		BIT(5)
#define VIA_VT1632_HEN		BIT(4)
#define VIA_VT1632_DSEL		BIT(3)
#define VIA_VT1632_BSEL		BIT(2)
#define VIA_VT1632_EDGE		BIT(1)
#define VIA_VT1632_PDB		BIT(0)


static void via_vt1632_power(struct drm_device *dev,
				struct i2c_adapter *i2c_bus,
				bool power_state)
{
	u8 buf;
	u8 power_bit;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	via_i2c_readbytes(i2c_bus, 0x08, 0x08, &buf, 1);
	power_bit = power_state ? VIA_VT1632_PDB : 0x00;
	buf &= ~power_bit;
	buf |= power_bit;
	via_i2c_writebytes(i2c_bus, 0x08, 0x08, &buf, 1);
	drm_dbg_kms(dev, "VT1632 (DVI) Power: %s\n",
			power_state ? "On" : "Off");

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}


static bool via_vt1632_sense(struct drm_device *dev,
				struct i2c_adapter *i2c_bus)
{
	u8 buf;
	bool rx_detected = false;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	via_i2c_readbytes(i2c_bus, 0x08, 0x09, &buf, 1);
	if (buf & BIT(2)) {
		rx_detected = true;
	}

	drm_dbg_kms(dev, "VT1632 (DVI) Connector Sense: %s\n",
			rx_detected ? "Connected" : "Not Connected");

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
	return rx_detected;
}

static void via_vt1632_display_registers(struct drm_device *dev,
					struct i2c_adapter *i2c_bus)
{
	uint8_t i;
	u8 buf;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	drm_dbg_kms(dev, "VT1632(A) Registers:\n");
	for (i = 0; i < 0x10; i++) {
		via_i2c_readbytes(i2c_bus, 0x08, i, &buf, 1);
		drm_dbg_kms(dev, "0x%02x: 0x%02x\n", i, buf);
	}

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

static void via_vt1632_init_registers(struct drm_device *dev,
					struct i2c_adapter *i2c_bus)
{
	u8 buf;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	/*
	 * For Wyse Cx0 thin client VX855 chipset DVP1 (Digital Video
	 * Port 1), use 12-bit mode with dual edge transfer, along
	 * with rising edge data capture first mode. This is likely
	 * true for CX700, VX700, VX800, and VX900 chipsets as well.
	 */
	buf = VIA_VT1632_VEN | VIA_VT1632_HEN |
		VIA_VT1632_DSEL |
		VIA_VT1632_EDGE | VIA_VT1632_PDB;
	via_i2c_writebytes(i2c_bus, 0x08, 0x08, &buf, 1);

	/*
	 * Route receiver detect bit (Offset 0x09[2]) as the output
	 * of MSEN pin.
	 */
	buf = BIT(5);
	via_i2c_writebytes(i2c_bus, 0x08, 0x09, &buf, 1);

	/*
	 * Turning on deskew feature caused screen display issues.
	 * This was observed with Wyse Cx0.
	 */
	buf = 0x00;
	via_i2c_writebytes(i2c_bus, 0x08, 0x0a, &buf, 1);

	/*
	 * While VIA Technologies VT1632A datasheet insists on setting
	 * this register to 0x89 as the recommended setting, in
	 * practice, this leads to a blank screen on the display with
	 * Wyse Cx0. According to Silicon Image SiI 164 datasheet
	 * (VT1632(A) is a pin and mostly register compatible chip),
	 * offset 0x0C is for PLL filter enable, PLL filter setting,
	 * and continuous SYNC enable bits. All of these are turned
	 * off for proper operation.
	 */
	buf = 0x00;
	via_i2c_writebytes(i2c_bus, 0x08, 0x0c, &buf, 1);

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}


static const struct drm_encoder_funcs via_vt1632_drm_encoder_funcs = {
	.destroy = via_encoder_destroy,
};

static void via_vt1632_dpms(struct drm_encoder *encoder, int mode)
{
	struct via_encoder *enc = container_of(encoder,
					struct via_encoder, base);
	struct drm_device *dev = encoder->dev;
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	struct i2c_adapter *i2c_bus;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	if (enc->i2c_bus & VIA_I2C_BUS1) {
		i2c_bus = via_find_ddc_bus(0x26);
	} else if (enc->i2c_bus & VIA_I2C_BUS2) {
		i2c_bus = via_find_ddc_bus(0x31);
	} else if (enc->i2c_bus & VIA_I2C_BUS3) {
		i2c_bus = via_find_ddc_bus(0x25);
	} else if (enc->i2c_bus & VIA_I2C_BUS4) {
		i2c_bus = via_find_ddc_bus(0x2c);
	} else if (enc->i2c_bus & VIA_I2C_BUS5) {
		i2c_bus = via_find_ddc_bus(0x3d);
	} else {
		i2c_bus = NULL;
		goto exit;
	}

	via_vt1632_display_registers(dev, i2c_bus);
	switch (mode) {
	case DRM_MODE_DPMS_ON:
		via_vt1632_power(dev, i2c_bus, true);
		via_transmitter_io_pad_state(dev_priv, enc->di_port, true);
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		via_vt1632_power(dev, i2c_bus, false);
		via_transmitter_io_pad_state(dev_priv, enc->di_port, false);
		break;
	default:
		drm_err(dev, "Bad DPMS mode.");
		break;
	}

exit:
	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

static bool via_vt1632_mode_fixup(struct drm_encoder *encoder,
				const struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	drm_mode_set_crtcinfo(adjusted_mode, 0);

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
	return true;
}

static void via_vt1632_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	struct via_crtc *iga = container_of(encoder->crtc, struct via_crtc, base);
	struct via_encoder *enc = container_of(encoder,
					struct via_encoder, base);
	struct drm_device *dev = encoder->dev;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	struct i2c_adapter *i2c_bus;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	if (enc->i2c_bus & VIA_I2C_BUS1) {
		i2c_bus = via_find_ddc_bus(0x26);
	} else if (enc->i2c_bus & VIA_I2C_BUS2) {
		i2c_bus = via_find_ddc_bus(0x31);
	} else if (enc->i2c_bus & VIA_I2C_BUS3) {
		i2c_bus = via_find_ddc_bus(0x25);
	} else if (enc->i2c_bus & VIA_I2C_BUS4) {
		i2c_bus = via_find_ddc_bus(0x2c);
	} else if (enc->i2c_bus & VIA_I2C_BUS5) {
		i2c_bus = via_find_ddc_bus(0x3d);
	} else {
		i2c_bus = NULL;
		goto exit;
	}

	via_transmitter_clock_drive_strength(dev_priv, enc->di_port, 0x03);
	via_transmitter_data_drive_strength(dev_priv, enc->di_port, 0x03);
	via_transmitter_io_pad_state(dev_priv, enc->di_port, true);
	if (pdev->device == PCI_DEVICE_ID_VIA_CLE266_GFX) {
		via_clock_source(dev_priv, enc->di_port, true);
	}

	via_vt1632_display_registers(dev, i2c_bus);
	via_vt1632_init_registers(dev, i2c_bus);
	via_vt1632_display_registers(dev, i2c_bus);

	via_transmitter_display_source(dev_priv, enc->di_port, iga->index);
exit:

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

static void via_vt1632_prepare(struct drm_encoder *encoder)
{
	struct via_encoder *enc = container_of(encoder,
					struct via_encoder, base);
	struct drm_device *dev = encoder->dev;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	struct i2c_adapter *i2c_bus;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	if (enc->i2c_bus & VIA_I2C_BUS1) {
		i2c_bus = via_find_ddc_bus(0x26);
	} else if (enc->i2c_bus & VIA_I2C_BUS2) {
		i2c_bus = via_find_ddc_bus(0x31);
	} else if (enc->i2c_bus & VIA_I2C_BUS3) {
		i2c_bus = via_find_ddc_bus(0x25);
	} else if (enc->i2c_bus & VIA_I2C_BUS4) {
		i2c_bus = via_find_ddc_bus(0x2c);
	} else if (enc->i2c_bus & VIA_I2C_BUS5) {
		i2c_bus = via_find_ddc_bus(0x3d);
	} else {
		i2c_bus = NULL;
		goto exit;
	}

	via_vt1632_power(dev, i2c_bus, false);
	via_transmitter_io_pad_state(dev_priv, enc->di_port, false);
	if (pdev->device == PCI_DEVICE_ID_VIA_CLE266_GFX) {
		via_output_enable(dev_priv, enc->di_port, false);
	}

exit:
	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

static void via_vt1632_commit(struct drm_encoder *encoder)
{
	struct via_encoder *enc = container_of(encoder,
					struct via_encoder, base);
	struct drm_device *dev = encoder->dev;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	struct i2c_adapter *i2c_bus;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	if (enc->i2c_bus & VIA_I2C_BUS1) {
		i2c_bus = via_find_ddc_bus(0x26);
	} else if (enc->i2c_bus & VIA_I2C_BUS2) {
		i2c_bus = via_find_ddc_bus(0x31);
	} else if (enc->i2c_bus & VIA_I2C_BUS3) {
		i2c_bus = via_find_ddc_bus(0x25);
	} else if (enc->i2c_bus & VIA_I2C_BUS4) {
		i2c_bus = via_find_ddc_bus(0x2c);
	} else if (enc->i2c_bus & VIA_I2C_BUS5) {
		i2c_bus = via_find_ddc_bus(0x3d);
	} else {
		i2c_bus = NULL;
		goto exit;
	}

	via_vt1632_power(dev, i2c_bus, true);
	via_transmitter_io_pad_state(dev_priv, enc->di_port, true);
	if (pdev->device == PCI_DEVICE_ID_VIA_CLE266_GFX) {
		via_output_enable(dev_priv, enc->di_port, true);
	}

exit:
	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

static void via_vt1632_disable(struct drm_encoder *encoder)
{
	struct via_encoder *enc = container_of(encoder,
					struct via_encoder, base);
	struct drm_device *dev = encoder->dev;
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	struct i2c_adapter *i2c_bus;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	if (enc->i2c_bus & VIA_I2C_BUS1) {
		i2c_bus = via_find_ddc_bus(0x26);
	} else if (enc->i2c_bus & VIA_I2C_BUS2) {
		i2c_bus = via_find_ddc_bus(0x31);
	} else if (enc->i2c_bus & VIA_I2C_BUS3) {
		i2c_bus = via_find_ddc_bus(0x25);
	} else if (enc->i2c_bus & VIA_I2C_BUS4) {
		i2c_bus = via_find_ddc_bus(0x2c);
	} else if (enc->i2c_bus & VIA_I2C_BUS5) {
		i2c_bus = via_find_ddc_bus(0x3d);
	} else {
		i2c_bus = NULL;
		goto exit;
	}

	via_vt1632_power(dev, i2c_bus, false);
	via_transmitter_io_pad_state(dev_priv, enc->di_port, false);
exit:
	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}


static const struct drm_encoder_helper_funcs
via_vt1632_drm_encoder_helper_funcs = {
	.dpms = via_vt1632_dpms,
	.mode_fixup = via_vt1632_mode_fixup,
	.mode_set = via_vt1632_mode_set,
	.prepare = via_vt1632_prepare,
	.commit = via_vt1632_commit,
	.disable = via_vt1632_disable,
};


static enum drm_connector_status via_vt1632_detect(
					struct drm_connector *connector,
					bool force)
{
	struct drm_device *dev = connector->dev;
	struct via_connector *con = container_of(connector,
					struct via_connector, base);
	struct i2c_adapter *i2c_bus;
	enum drm_connector_status ret = connector_status_disconnected;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	if (con->i2c_bus & VIA_I2C_BUS1) {
		i2c_bus = via_find_ddc_bus(0x26);
	} else if (con->i2c_bus & VIA_I2C_BUS2) {
		i2c_bus = via_find_ddc_bus(0x31);
	} else if (con->i2c_bus & VIA_I2C_BUS3) {
		i2c_bus = via_find_ddc_bus(0x25);
	} else if (con->i2c_bus & VIA_I2C_BUS4) {
		i2c_bus = via_find_ddc_bus(0x2c);
	} else if (con->i2c_bus & VIA_I2C_BUS5) {
		i2c_bus = via_find_ddc_bus(0x3d);
	} else {
		i2c_bus = NULL;
		goto exit;
	}

	if (via_vt1632_sense(dev, i2c_bus)) {
		ret = connector_status_connected;
		drm_dbg_kms(dev, "DVI detected.\n");
	}

exit:
	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
	return ret;
}

static const struct drm_connector_funcs via_vt1632_drm_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = via_vt1632_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = via_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state =
			drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state =
			drm_atomic_helper_connector_destroy_state,
};


int via_vt1632_mode_valid(struct drm_connector *connector,
					struct drm_display_mode *mode)
{
	struct drm_device *dev = connector->dev;
	struct via_connector *con = container_of(connector,
					struct via_connector, base);
	struct i2c_adapter *i2c_bus;
	u8 buf;
	uint32_t low_freq_limit, high_freq_limit;
	int ret;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	if (con->i2c_bus & VIA_I2C_BUS1) {
		i2c_bus = via_find_ddc_bus(0x26);
	} else if (con->i2c_bus & VIA_I2C_BUS2) {
		i2c_bus = via_find_ddc_bus(0x31);
	} else if (con->i2c_bus & VIA_I2C_BUS3) {
		i2c_bus = via_find_ddc_bus(0x25);
	} else if (con->i2c_bus & VIA_I2C_BUS4) {
		i2c_bus = via_find_ddc_bus(0x2c);
	} else if (con->i2c_bus & VIA_I2C_BUS5) {
		i2c_bus = via_find_ddc_bus(0x3d);
	} else {
		i2c_bus = NULL;
		ret = MODE_ERROR;
		goto exit;
	}

	via_i2c_readbytes(i2c_bus, 0x08, 0x06, &buf, 1);
	low_freq_limit = buf * 1000;
	via_i2c_readbytes(i2c_bus, 0x08, 0x07, &buf, 1);
	high_freq_limit = (buf + 65) * 1000;
	drm_dbg_kms(dev, "Low Frequency Limit: %u KHz\n", low_freq_limit);
	drm_dbg_kms(dev, "High Frequency Limit: %u KHz\n", high_freq_limit);

	if (mode->clock < low_freq_limit) {
		ret = MODE_CLOCK_LOW;
		goto exit;
	}

	if (mode->clock > high_freq_limit) {
		ret = MODE_CLOCK_HIGH;
		goto exit;
	}

	ret = MODE_OK;
exit:
	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
	return ret;
}

static int via_vt1632_get_modes(struct drm_connector *connector)
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
	} else if (con->i2c_bus & VIA_I2C_BUS2) {
		i2c_bus = via_find_ddc_bus(0x31);
	} else if (con->i2c_bus & VIA_I2C_BUS3) {
		i2c_bus = via_find_ddc_bus(0x25);
	} else if (con->i2c_bus & VIA_I2C_BUS4) {
		i2c_bus = via_find_ddc_bus(0x2c);
	} else if (con->i2c_bus & VIA_I2C_BUS5) {
		i2c_bus = via_find_ddc_bus(0x3d);
	} else {
		i2c_bus = NULL;
		goto exit;
	}

	edid = drm_get_edid(&con->base, i2c_bus);
	if (edid) {
		if (edid->input & DRM_EDID_INPUT_DIGITAL) {
			drm_connector_update_edid_property(connector, edid);
			count = drm_add_edid_modes(connector, edid);
			drm_dbg_kms(dev, "DVI EDID information was obtained.\n");
		}

		kfree(edid);
	}

exit:
	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
	return count;
}

static const struct drm_connector_helper_funcs
via_vt1632_drm_connector_helper_funcs = {
	.mode_valid = via_vt1632_mode_valid,
	.get_modes = via_vt1632_get_modes,
};

bool via_vt1632_probe(struct drm_device *dev,
			struct i2c_adapter *i2c_bus)
{
	u8 buf;
	u16 vendor_id, device_id, revision;
	bool device_detected = false;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	via_i2c_readbytes(i2c_bus, 0x08, 0x00, &buf, 1);
	vendor_id = buf;
	via_i2c_readbytes(i2c_bus, 0x08, 0x01, &buf, 1);
	vendor_id |= (buf << 8);
	drm_dbg_kms(dev, "Vendor ID: %x\n", vendor_id);
	via_i2c_readbytes(i2c_bus, 0x08, 0x02, &buf, 1);
	device_id = buf;
	via_i2c_readbytes(i2c_bus, 0x08, 0x03, &buf, 1);
	device_id |= (buf << 8);
	drm_dbg_kms(dev, "Device ID: %x\n", device_id);
	via_i2c_readbytes(i2c_bus, 0x08, 0x04, &buf, 1);
	revision = buf;
	drm_dbg_kms(dev, "Revision: %x\n", revision);

	if ((vendor_id != 0x1106) || (device_id != 0x3192)) {
		goto exit;
	}

	device_detected = true;
exit:
	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
	return device_detected;
}

void via_vt1632_init(struct drm_device *dev)
{
	struct via_connector *con;
	struct via_encoder *enc;
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	if (!dev_priv->ext_tmds_presence) {
		goto exit;
	}

	enc = kzalloc(sizeof(*enc) + sizeof(*con), GFP_KERNEL);
	if (!enc) {
		drm_err(dev, "Failed to allocate connector "
				"and encoder.\n");
		goto exit;
	}

	drm_encoder_init(dev, &enc->base, &via_vt1632_drm_encoder_funcs,
						DRM_MODE_ENCODER_TMDS, NULL);
	drm_encoder_helper_add(&enc->base,
					&via_vt1632_drm_encoder_helper_funcs);

	enc->base.possible_crtcs = BIT(1) | BIT(0);
	enc->base.possible_clones = 0;

	enc->i2c_bus = dev_priv->ext_tmds_i2c_bus;
	enc->di_port = dev_priv->ext_tmds_di_port;

	/* Increment the number of DVI connectors. */
	dev_priv->number_dvi++;


	con = &enc->cons[0];

	drm_connector_init(dev, &con->base, &via_vt1632_drm_connector_funcs,
				DRM_MODE_CONNECTOR_DVID);
	drm_connector_helper_add(&con->base,
				&via_vt1632_drm_connector_helper_funcs);
	drm_connector_register(&con->base);

	con->base.doublescan_allowed = false;
	con->base.interlace_allowed = false;

	con->i2c_bus = dev_priv->ext_tmds_i2c_bus;

	INIT_LIST_HEAD(&con->props);
	drm_connector_attach_encoder(&con->base, &enc->base);
exit:
	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}
