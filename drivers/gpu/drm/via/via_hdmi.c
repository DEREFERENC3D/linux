/*
 * Copyright 2013 James Simmons. All Rights Reserved.
 * Copyright 1998-2012 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2012 S3 Graphics, Inc. All Rights Reserved.
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
 * James Simmons <jsimmons@infradead.org>
 */

#include <linux/delay.h>

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include "via_drv.h"


#define HDMI_AUDIO_ENABLED	BIT(0)

int via_hdmi_audio = 0;

MODULE_PARM_DESC(audio, "HDMI Audio enable (1 = enable)");
module_param_named(audio, via_hdmi_audio, int, 0444);

/*
 * Routines for controlling stuff on the HDMI port
 */
static const struct drm_encoder_funcs via_hdmi_enc_funcs = {
	.destroy = via_encoder_destroy,
};

static void via_hdmi_enc_dpms(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	switch (mode) {
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_OFF:
		/* disable HDMI */
		VIA_WRITE_MASK(0xC280, 0x0, 0x2);
		break;

	case DRM_MODE_DPMS_ON:
	default:
		/* enable band gap */
		VIA_WRITE_MASK(0xC740, BIT(0), BIT(0));
		/* enable video */
		VIA_WRITE_MASK(0xC640, BIT(3), BIT(3));
		/* enable HDMI */
		VIA_WRITE_MASK(0xC280, BIT(1), BIT(1));
		break;
	}
}

static bool via_hdmi_enc_mode_fixup(struct drm_encoder *encoder,
				const struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	uint32_t panelHSyncTime = 0, panelHBlankStart = 0, newHBlankStart = 0;
	uint32_t panelVSyncTime = 0, panelVBlankStart = 0, newVBlankStart = 0;
	uint32_t left_border = 0, right_border = 0;
	uint32_t top_border = 0, bottom_border = 0;

	if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE) {
		/* when interlace mode, we should consider halve vertical
		 * timings. */
		panelHSyncTime = adjusted_mode->hsync_end -
					adjusted_mode->hsync_start;
		panelVSyncTime = adjusted_mode->vsync_end / 2 -
					adjusted_mode->vsync_start / 2;
		panelHBlankStart = adjusted_mode->hdisplay;
		panelVBlankStart = adjusted_mode->vdisplay / 2;
		newHBlankStart = adjusted_mode->hdisplay - left_border;
		newVBlankStart = adjusted_mode->vdisplay / 2 - top_border;

		adjusted_mode->hdisplay =
			adjusted_mode->hdisplay - left_border - right_border;
		adjusted_mode->hsync_start =
			(adjusted_mode->hsync_start - panelHBlankStart) +
				newHBlankStart;
		adjusted_mode->hsync_end =
			adjusted_mode->hsync_start + panelHSyncTime;

		adjusted_mode->vdisplay = adjusted_mode->vdisplay / 2 -
						top_border - bottom_border;
		adjusted_mode->vsync_start =
				(adjusted_mode->vsync_start / 2 - panelVBlankStart) +
				newVBlankStart;
		adjusted_mode->vsync_end =
				adjusted_mode->vsync_start + panelVSyncTime;
	} else {
		panelHSyncTime =
			adjusted_mode->hsync_end - adjusted_mode->hsync_start;
		panelVSyncTime =
			adjusted_mode->vsync_end - adjusted_mode->vsync_start;
		panelHBlankStart = adjusted_mode->hdisplay;
		panelVBlankStart = adjusted_mode->vdisplay;
		newHBlankStart = adjusted_mode->hdisplay - left_border;
		newVBlankStart = adjusted_mode->vdisplay - top_border;

		adjusted_mode->hdisplay =
			adjusted_mode->hdisplay - left_border - right_border;
		adjusted_mode->hsync_start =
			(adjusted_mode->hsync_start - panelHBlankStart) +
			newHBlankStart;
		adjusted_mode->hsync_end =
			adjusted_mode->hsync_start + panelHSyncTime;

		adjusted_mode->vdisplay =
			adjusted_mode->vdisplay - top_border - bottom_border;
		adjusted_mode->vsync_start =
			(adjusted_mode->vsync_start - panelVBlankStart) +
			newVBlankStart;
		adjusted_mode->vsync_end =
			adjusted_mode->vsync_start + panelVSyncTime;
	}

	/* Adjust crtc H and V */
	adjusted_mode->crtc_hdisplay = adjusted_mode->hdisplay;
	adjusted_mode->crtc_hblank_start = newHBlankStart;
	adjusted_mode->crtc_hblank_end =
	adjusted_mode->crtc_htotal - left_border;
	adjusted_mode->crtc_hsync_start = adjusted_mode->hsync_start;
	adjusted_mode->crtc_hsync_end = adjusted_mode->hsync_end;

	adjusted_mode->crtc_vdisplay = adjusted_mode->vdisplay;
	adjusted_mode->crtc_vblank_start = newVBlankStart;
	adjusted_mode->crtc_vblank_end =
	adjusted_mode->crtc_vtotal - top_border;
	adjusted_mode->crtc_vsync_start = adjusted_mode->vsync_start;
	adjusted_mode->crtc_vsync_end = adjusted_mode->vsync_end;

	drm_mode_set_crtcinfo(adjusted_mode, 0);
	return true;
}

static void via_hdmi_native_mode_set(struct via_crtc *iga,
					struct drm_display_mode *mode,
					bool audio_off)
{
	struct drm_device *dev = iga->base.dev;
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	u32 reg_c280, reg_c284;
	int max_packet, delay;
	u8 value = BIT(0);

	/* 135MHz ~ 270MHz */
	if (mode->clock >= 135000)
		VIA_WRITE_MASK(DP_EPHY_PLL_REG, 0x00000000, 0xC0000000);
	/* 67.5MHz ~ <135MHz */
	else if (mode->clock >= 67500)
		VIA_WRITE_MASK(DP_EPHY_PLL_REG, 0x40000000, 0xC0000000);
	/* 33.75MHz ~ <67.5MHz */
	else if (mode->clock >= 33750)
		VIA_WRITE_MASK(DP_EPHY_PLL_REG, 0x80000000, 0xC0000000);
	/* 25MHz ~ <33.75MHz */
	else
		VIA_WRITE_MASK(DP_EPHY_PLL_REG, 0xC0000000, 0xC0000000);

	/* touch C282 when init HDMI by mode 720x576, 720x480,
	 * or other modes */
	if ((mode->hdisplay == 720) && (mode->vdisplay == 576))
		VIA_WRITE(0xC280, 0x18232402);
	else if ((mode->hdisplay == 720) && (mode->vdisplay == 480))
		VIA_WRITE(0xC280, 0x181f2402);
	else
		VIA_WRITE(0xC280, 0x18330002);

	/* init C280 */
	reg_c280 = 0x18000002 | (VIA_READ(0xC280) & 0x40);
	/* sync polarity */
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		reg_c280 |= BIT(10);

	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		reg_c280 |= BIT(13);

	/* calculate correct delay of regC280[22:16] */
	if ((mode->crtc_hsync_start - mode->crtc_hdisplay) > (58 - 11))
		delay = 0;
	else
		delay = 58 - (mode->crtc_hsync_start - mode->crtc_hdisplay) - 11;

	/* calculate max_packet */
	max_packet = (mode->crtc_hblank_end - mode->crtc_hsync_start - 16 - 11 - delay) / 32;
	if (0 == delay)
		delay = mode->crtc_hblank_end - mode->crtc_hsync_start - (32 * max_packet + 16 + 11);

	reg_c280 |= (delay << 16);
	VIA_WRITE(0xC280, reg_c280);
	reg_c284 = 0x00000102 | (max_packet << 28);

	/* power down to reset */
	VIA_WRITE_MASK(0xC740, 0x00000000, 0x06000000);
	/* enable DP data pass */
	VIA_WRITE_MASK(DP_DATA_PASS_ENABLE_REG, 0x00000001, 0x00000001);
	/* select HDMI mode */
	VIA_WRITE_MASK(0xC748, 0, BIT(0));
	/* enable HDMI with HDMI mode */
	VIA_WRITE_MASK(0xC280, 0x00, 0x40);
	/* select AC mode */
	VIA_WRITE_MASK(0xC74C, 0x40, 0x40);
	/* enable InfoFrame */
	VIA_WRITE(0xC284, reg_c284);
	/* set status of Lane0~3 */
	VIA_WRITE_MASK(0xC744, 0x00FFFF82, 0x00FFFF82);
	VIA_WRITE(0xC0B4, 0x92000000);
	/* enable audio packet */
	if (!audio_off)
		VIA_WRITE_MASK(0xC294, 0x10000000, 0x10000000);
	/* enable InfoFrame */
	VIA_WRITE(0xC284, reg_c284);
	VIA_WRITE_MASK(0xC740, 0x1E4CBE7F, 0x3FFFFFFF);
	VIA_WRITE_MASK(0xC748, 0x84509180, 0x001FFFFF);
	/* Select PHY Function as HDMI */
	/* Select HDTV0 source */
	if (iga->index)
		value |= BIT(1);
	svga_wcrt_mask(VGABASE, 0xFF, value, BIT(1) | BIT(0));
}

static void via_hdmi_enc_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	struct via_encoder *enc = container_of(encoder, struct via_encoder, base);
	struct via_crtc *iga = container_of(encoder->crtc, struct via_crtc, base);
	struct drm_connector *connector = NULL, *con;
	struct drm_device *dev = encoder->dev;
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	list_for_each_entry(con, &dev->mode_config.connector_list, head) {
		if (encoder == con->encoder) {
			connector = con;
			break;
		}
	}

	if (!connector) {
		DRM_INFO("HDMI encoder is not used by any connector\n");
		return;
	}

	if (connector->connector_type == DRM_MODE_CONNECTOR_HDMIA) {
		struct via_connector *con = container_of(connector, struct via_connector, base);
		bool audio_off = (con->flags & HDMI_AUDIO_ENABLED);

		if (enc->di_port == VIA_DI_PORT_NONE)
			via_hdmi_native_mode_set(iga, adjusted_mode, audio_off);

		if (!iga->index)
			via_load_crtc_pixel_timing(encoder->crtc, adjusted_mode);

		/* Set Hsync Offset, delay one clock (To meet 861-D spec.) */
		svga_wcrt_mask(VGABASE, 0x8A, 0x01, 0x7);

		/* If CR8A +1, HSyc must -1 */
		vga_wcrt(VGABASE, 0x56, vga_rcrt(VGABASE, 0x56) - 1);
		vga_wcrt(VGABASE, 0x57, vga_rcrt(VGABASE, 0x57) - 1);

		if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE) {
			if (iga->index) {
				/* FIXME VIA where do you get this value from ??? */
				u32 v_sync_adjust = 0;

				svga_wcrt_mask(VGABASE, 0xAB, v_sync_adjust & 0xFF, 0xFF);
				svga_wcrt_mask(VGABASE, 0xAC, (v_sync_adjust & 0x700) >> 8, 0x07);
			}
		} else { /* non-interlace, clear interlace setting. */
			if (iga->index) {
				vga_wcrt(VGABASE, 0xFB, 0);
				svga_wcrt_mask(VGABASE, 0xFC, 0, 0x07);
			}
		}
	} else if (connector->connector_type == DRM_MODE_CONNECTOR_DVID) {
		/* 135MHz ~ 270MHz */
		if (mode->clock >= 135000)
			VIA_WRITE_MASK(DP_EPHY_PLL_REG, 0x00000000, 0xC0000000);
		/* 67.5MHz ~ < 135MHz */
		else if (mode->clock >= 67500)
			VIA_WRITE_MASK(DP_EPHY_PLL_REG, 0x40000000, 0xC0000000);
		/* 33.75MHz ~ < 67.5MHz */
		else if (mode->clock >= 33750)
			VIA_WRITE_MASK(DP_EPHY_PLL_REG, 0x80000000, 0xC0000000);
		/* 25MHz ~ < 33.75MHz */
		else
			VIA_WRITE_MASK(DP_EPHY_PLL_REG, 0xC0000000, 0xC0000000);

		/* Power down TPLL to reset */
		VIA_WRITE_MASK(DP_EPHY_PLL_REG, 0x00000000, 0x06000000);
		/* Enable DP data pass */
		VIA_WRITE_MASK(DP_DATA_PASS_ENABLE_REG, 0x00000001, 0x00000001);
		/* Select EPHY as HDMI mode */
		VIA_WRITE_MASK(DP_EPHY_MISC_PWR_REG, 0, BIT(0));
		/* Enable HDMI with DVI mode */
		VIA_WRITE_MASK(0xC280, 0x40, 0x40);
		/* select AC mode */
		VIA_WRITE_MASK(0xC74C, 0x40, 0x40);
		/* Set status of Lane0~3 */
		VIA_WRITE_MASK(0xC744, 0x00FFFF00, 0x00FFFF00);
		/* Disable InfoFrame */
		VIA_WRITE_MASK(0xC284, 0x00000000, 0x00000002);
		/* EPHY Control Register */
		VIA_WRITE_MASK(DP_EPHY_PLL_REG, 0x1EC46E6F, 0x3FFFFFFF);
		/* Select PHY Function as HDMI */
		svga_wcrt_mask(VGABASE, 0xFF, BIT(0), BIT(0));
		/* Select HDTV0 source */
		if (!iga->index)
			svga_wcrt_mask(VGABASE, 0xFF, 0, BIT(1));
		else
			svga_wcrt_mask(VGABASE, 0xFF, BIT(1), BIT(1));

		/* in 640x480 case, MPLL is different */
		/* For VT3410 internal transmitter 640x480 issue */
		if (mode->hdisplay == 640 && mode->vdisplay == 480) {
			VIA_WRITE(DP_EPHY_PLL_REG, 0xD8C29E6F);
			VIA_WRITE(DP_EPHY_PLL_REG, 0xDEC29E6F);
		}
	}
}

static const struct drm_encoder_helper_funcs via_hdmi_enc_helper_funcs = {
	.dpms = via_hdmi_enc_dpms,
	.mode_fixup = via_hdmi_enc_mode_fixup,
	.mode_set = via_hdmi_enc_mode_set,
};

static unsigned int via_check_hdmi_i2c_status(struct via_drm_priv *dev_priv,
						unsigned int check_bits,
						unsigned int condition)
{
	unsigned int status = true, max = 50, loop = 0;

	if (condition) {
		while ((VIA_READ(0xC0B8) & check_bits) && loop < max) {
			/* delay 20 us */
			udelay(20);

			if (++loop == max)
				status = false;
		}
	} else {
		while (!(VIA_READ(0xC0B8) & check_bits) && loop < max) {
			/* delay 20 us */
			udelay(20);

			if (++loop == max)
				status = false;
		}
	}
	return status;
}

unsigned int via_ddc_read_bytes_by_hdmi(struct via_drm_priv *dev_priv,
					unsigned int offset,
					unsigned char *block)
{
	unsigned int status = true, temp = 0, i;

	/* Enable DDC */
	VIA_WRITE_MASK(0xC000, 0x00000001, 0x00000001);
	VIA_WRITE(0xC0C4, (VIA_READ(0xC0C4) & 0xFC7FFFFF) | 0x00800000);
	VIA_WRITE(0xC0B8, 0x00000001);

	/* START */
	VIA_WRITE(0xC0B8, 0x0011);
	VIA_WRITE(0xC0B8, 0x0019);
	if (status)
		status = via_check_hdmi_i2c_status(dev_priv, 0x0018, true);

	/* Slave Address */
	temp = 0xA0;
	temp <<= 16;
	temp |= VIA_READ(0xC0B4) & 0xFF00FFFF;
	VIA_WRITE(0xC0B4, temp);
	VIA_WRITE(0xC0B8, 0x0009);
	if (status)
		status = via_check_hdmi_i2c_status(dev_priv, 0x0008, true);

	/* Offset */
	temp = offset;
	temp <<= 16;
	temp |= VIA_READ(0xC0B4) & 0xFF00FFFF;
	VIA_WRITE(0xC0B4, temp);
	VIA_WRITE(0xC0B8, 0x0009);
	if (status)
		status = via_check_hdmi_i2c_status(dev_priv, 0x0008, true);

	/* START */
	VIA_WRITE(0xC0B8, 0x0011);
	VIA_WRITE(0xC0B8, 0x0019);
	if (status)
		status = via_check_hdmi_i2c_status(dev_priv, 0x0018, true);

	/* Slave Address + 1 */
	temp = 0xA1;
	temp <<= 16;
	temp |= VIA_READ(0xC0B4) & 0xFF00FFFF;
	VIA_WRITE(0xC0B4, temp);
	VIA_WRITE(0xC0B8, 0x0009);
	if (status)
		status = via_check_hdmi_i2c_status(dev_priv, 0x0008, true);

	for (i = 0; i < EDID_LENGTH; i++) {
		/* Read Data */
		VIA_WRITE(0xC0B8, 0x0009);
		via_check_hdmi_i2c_status(dev_priv, 0x0008, true);
		via_check_hdmi_i2c_status(dev_priv, 0x0080, false);
		*block++ = (unsigned char) ((VIA_READ(0xC0B4) & 0x0000FF00) >> 8);
		VIA_WRITE(0xC0B8, (VIA_READ(0xC0B8) & ~0x80));
	}

	/* STOP */
	VIA_WRITE(0xC0B8, 0x0021);
	VIA_WRITE(0xC0B8, 0x0029);

	status = via_check_hdmi_i2c_status(dev_priv, 0x0828, true);
	if (!status) {
		/* Reset */
		VIA_WRITE_MASK(0xC0C4, 0x00000080, 0x00000080);
		VIA_WRITE_MASK(0xC0C4, 0x00000000, 0x00000080);
	}
	return status;
}

struct edid* via_hdmi_get_edid(struct drm_connector *connector)
{
	bool print_bad_edid = !connector->bad_edid_counter || (drm_debug_enabled(DRM_UT_KMS));
	struct drm_device *dev = connector->dev;
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	struct edid *edid = NULL;
	int i, j = 0;
	u8 *block;

	/* Clear out old EDID block */
	drm_connector_update_edid_property(connector, edid);

	block = kmalloc(EDID_LENGTH, GFP_KERNEL);
	if (!block)
		return edid;

	/* base block fetch */
	for (i = 0; i < 4; i++) {
		if (!via_ddc_read_bytes_by_hdmi(dev_priv, 0, block))
			goto out;

		if (drm_edid_block_valid(block, 0, print_bad_edid, NULL))
			break;

		if (i == 0 && !memchr_inv(block, 0, EDID_LENGTH)) {
			connector->null_edid_counter++;
			goto carp;
		}
	}
	if (i == 4)
		goto carp;

	/* parse the extensions if present */
	if (block[0x7e]) {
		u8 *new = krealloc(block, (block[0x7e] + 1) * EDID_LENGTH, GFP_KERNEL);
		int valid_extensions = 0, offset = 0;

		if (!new)
			goto out;
		block = new;

		for (j = 1; j <= block[0x7e]; j++) {
			for (i = 0; i < 4; i++) {
				offset = (valid_extensions + 1) * EDID_LENGTH;
				new = block + offset;

				if (!via_ddc_read_bytes_by_hdmi(dev_priv,
								 offset, new))
					goto out;

				if (drm_edid_block_valid(new, j, print_bad_edid, NULL)) {
					valid_extensions++;
					break;
				}
			}

			if (i == 4 && print_bad_edid) {
				dev_warn(connector->dev->dev,
					"%s: Ignoring invalid EDID block %d.\n",
					connector->name, j);

				connector->bad_edid_counter++;
			}
		}

		if (valid_extensions != block[0x7e]) {
			block[EDID_LENGTH - 1] += block[0x7e] - valid_extensions;
			block[0x7e] = valid_extensions;

			new = krealloc(block, (valid_extensions + 1) * EDID_LENGTH, GFP_KERNEL);
			if (!new)
				goto out;
			block = new;
		}
	}
	edid = (struct edid *) block;
	drm_connector_update_edid_property(connector, edid);
	return edid;

carp:
	if (print_bad_edid) {
		dev_warn(connector->dev->dev, "%s: EDID block %d invalid.\n",
			 connector->name, j);
	}
	connector->bad_edid_counter++;
out:
	kfree(block);
	return edid;
}

static enum drm_connector_status
via_hdmi_detect(struct drm_connector *connector, bool force)
{
	struct drm_device *dev = connector->dev;
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	enum drm_connector_status ret = connector_status_disconnected;
	u32 mm_c730 = VIA_READ(0xc730) & 0xC0000000;
	struct edid *edid = NULL;

	if (VIA_IRQ_DP_HOT_UNPLUG == mm_c730) {
		drm_connector_update_edid_property(connector, NULL);
		return ret;
	}

	edid = via_hdmi_get_edid(connector);
	if (edid) {
		if ((connector->connector_type != DRM_MODE_CONNECTOR_HDMIA) ^
			(drm_detect_hdmi_monitor(edid)))
			ret = connector_status_connected;
	}
	return ret;
}

static int via_hdmi_set_property(struct drm_connector *connector,
					struct drm_property *property,
					uint64_t value)
{
	struct drm_device *dev = connector->dev;

	if (property == dev->mode_config.dpms_property && connector->encoder)
		via_hdmi_enc_dpms(connector->encoder, (uint32_t)(value & 0xf));
	return 0;
}

static const struct drm_connector_funcs via_hdmi_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = via_hdmi_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = via_hdmi_set_property,
	.destroy = via_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state =
			drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state =
			drm_atomic_helper_connector_destroy_state,
};

static int via_hdmi_mode_valid(struct drm_connector *connector,
				struct drm_display_mode *mode)
{
	return MODE_OK;
}

int via_hdmi_get_modes(struct drm_connector *connector)
{
	struct edid *edid = via_hdmi_get_edid(connector);

	if (edid) {
		struct via_connector *con;

		if (edid->input & DRM_EDID_INPUT_DIGITAL) {
			con = container_of(connector, struct via_connector, base);

			if (via_hdmi_audio)
				con->flags |= drm_detect_monitor_audio(edid);
		}
	}
	return drm_add_edid_modes(connector, edid);
}

static const struct drm_connector_helper_funcs via_hdmi_connector_helper_funcs = {
	.mode_valid = via_hdmi_mode_valid,
	.get_modes = via_hdmi_get_modes,
};

void via_hdmi_init(struct drm_device *dev, u32 di_port)
{
	struct via_connector *dvi, *hdmi;
	struct via_encoder *enc;

	enc = kzalloc(sizeof(*enc) + 2 * sizeof(*hdmi), GFP_KERNEL);
	if (!enc) {
		DRM_ERROR("Failed to allocate connector and encoder\n");
		return;
	}
	hdmi = &enc->cons[0];
	dvi = &enc->cons[1];

	/* Setup the encoders and attach them */
	drm_encoder_init(dev, &enc->base, &via_hdmi_enc_funcs,
						DRM_MODE_ENCODER_TMDS, NULL);
	drm_encoder_helper_add(&enc->base, &via_hdmi_enc_helper_funcs);

	enc->base.possible_crtcs = BIT(1) | BIT(0);
	enc->base.possible_clones = 0;
	enc->di_port = di_port;

	/* Setup the HDMI connector */
	drm_connector_init(dev, &hdmi->base, &via_hdmi_connector_funcs,
				DRM_MODE_CONNECTOR_HDMIA);
	drm_connector_helper_add(&hdmi->base, &via_hdmi_connector_helper_funcs);
	drm_connector_register(&hdmi->base);

	hdmi->base.polled = DRM_CONNECTOR_POLL_HPD;
	hdmi->base.doublescan_allowed = false;
	hdmi->base.interlace_allowed = true;
	INIT_LIST_HEAD(&hdmi->props);

	drm_connector_attach_encoder(&hdmi->base, &enc->base);

	/* Setup the DVI connector */
	drm_connector_init(dev, &dvi->base, &via_hdmi_connector_funcs,
				DRM_MODE_CONNECTOR_DVID);
	drm_connector_helper_add(&dvi->base, &via_hdmi_connector_helper_funcs);
	drm_connector_register(&dvi->base);

	dvi->base.polled = DRM_CONNECTOR_POLL_HPD;
	dvi->base.doublescan_allowed = false;
	dvi->base.interlace_allowed = true;
	INIT_LIST_HEAD(&dvi->props);

	drm_connector_attach_encoder(&dvi->base, &enc->base);
}
