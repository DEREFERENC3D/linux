/*
 * Copyright © 2017-2018 Kevin Brace.
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

#include "via_drv.h"


void via_transmitter_io_pad_state(struct drm_device *dev,
						uint32_t di_port,
						bool io_pad_on)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	switch(di_port) {
	case VIA_DI_PORT_DIP0:
		via_dip0_set_io_pad_state(VGABASE,
					io_pad_on ? 0x03 : 0x00);
		break;
	case VIA_DI_PORT_DIP1:
		via_dip1_set_io_pad_state(VGABASE,
					io_pad_on ? 0x03 : 0x00);
		break;
	case VIA_DI_PORT_DVP0:
		via_dvp0_set_io_pad_state(VGABASE,
					io_pad_on ? 0x03 : 0x00);
		break;
	case VIA_DI_PORT_DVP1:
		via_dvp1_set_io_pad_state(VGABASE,
					io_pad_on ? 0x03 : 0x00);
		break;
	case VIA_DI_PORT_FPDPLOW:
		via_fpdp_low_set_io_pad_state(VGABASE,
					io_pad_on ? 0x03 : 0x00);
		break;
	case VIA_DI_PORT_FPDPHIGH:
		via_fpdp_high_set_io_pad_state(VGABASE,
					io_pad_on ? 0x03 : 0x00);
		break;
	case (VIA_DI_PORT_FPDPLOW |
		VIA_DI_PORT_FPDPHIGH):
		via_fpdp_low_set_io_pad_state(VGABASE,
					io_pad_on ? 0x03 : 0x00);
		via_fpdp_high_set_io_pad_state(VGABASE,
					io_pad_on ? 0x03 : 0x00);
		break;
	case VIA_DI_PORT_LVDS1:
		via_lvds1_set_io_pad_setting(VGABASE,
					io_pad_on ? 0x03 : 0x00);
		break;
	case VIA_DI_PORT_LVDS2:
		via_lvds2_set_io_pad_setting(VGABASE,
					io_pad_on ? 0x03 : 0x00);
		break;
	case (VIA_DI_PORT_LVDS1 |
		VIA_DI_PORT_LVDS2):
		via_lvds1_set_io_pad_setting(VGABASE,
					io_pad_on ? 0x03 : 0x00);
		via_lvds2_set_io_pad_setting(VGABASE,
					io_pad_on ? 0x03 : 0x00);
		break;
	default:
		break;
	}

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

void via_output_enable(struct drm_device *dev,
			uint32_t di_port, bool output_enable)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	switch(di_port) {
	case VIA_DI_PORT_DIP0:
		via_dip0_set_output_enable(VGABASE, output_enable);
		break;
	case VIA_DI_PORT_DIP1:
		via_dip1_set_output_enable(VGABASE, output_enable);
		break;
	default:
		break;
	}

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

void via_clock_source(struct drm_device *dev,
			uint32_t di_port, bool clock_source)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	switch(di_port) {
	case VIA_DI_PORT_DIP0:
		via_dip0_set_clock_source(VGABASE, clock_source);
		break;
	case VIA_DI_PORT_DIP1:
		via_dip1_set_clock_source(VGABASE, clock_source);
		break;
	default:
		break;
	}

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

void via_transmitter_clock_drive_strength(struct drm_device *dev,
						u32 di_port,
						u8 drive_strength)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	switch(di_port) {
	case VIA_DI_PORT_DVP0:
		via_dvp0_set_clock_drive_strength(VGABASE,
						drive_strength);
		break;
	case VIA_DI_PORT_DVP1:
		via_dvp1_set_clock_drive_strength(VGABASE,
						drive_strength);
		break;
	default:
		break;
	}

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

void via_transmitter_data_drive_strength(struct drm_device *dev,
					u32 di_port, u8 drive_strength)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	switch(di_port) {
	case VIA_DI_PORT_DVP0:
		via_dvp0_set_data_drive_strength(VGABASE,
						drive_strength);
		break;
	case VIA_DI_PORT_DVP1:
		via_dvp1_set_data_drive_strength(VGABASE,
						drive_strength);
		break;
	default:
		break;
	}

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}

void via_transmitter_display_source(struct drm_device *dev,
					u32 di_port, int index)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	u8 display_source = index & 0x01;

	drm_dbg_kms(dev, "Entered %s.\n", __func__);

	switch(di_port) {
	case VIA_DI_PORT_DIP0:
		via_dip0_set_display_source(VGABASE, display_source);
		break;
	case VIA_DI_PORT_DIP1:
		via_dip1_set_display_source(VGABASE, display_source);
		break;
	case VIA_DI_PORT_DVP0:
		via_dvp0_set_display_source(VGABASE, display_source);
		break;
	case VIA_DI_PORT_DVP1:
		via_dvp1_set_display_source(VGABASE, display_source);
		break;
	case VIA_DI_PORT_FPDPLOW:
		via_fpdp_low_set_display_source(VGABASE, display_source);
		via_dvp1_set_display_source(VGABASE, display_source);
		break;
	case VIA_DI_PORT_FPDPHIGH:
		via_fpdp_high_set_display_source(VGABASE, display_source);
		via_dvp0_set_display_source(VGABASE, display_source);
		break;
	case (VIA_DI_PORT_FPDPLOW |
		VIA_DI_PORT_FPDPHIGH):
		via_fpdp_low_set_display_source(VGABASE, display_source);
		via_fpdp_high_set_display_source(VGABASE, display_source);
		break;
	case VIA_DI_PORT_LVDS1:
		via_lvds1_set_display_source(VGABASE, display_source);
		break;
	case VIA_DI_PORT_LVDS2:
		via_lvds2_set_display_source(VGABASE, display_source);
		break;
	case (VIA_DI_PORT_LVDS1 |
		VIA_DI_PORT_LVDS2):
		via_lvds1_set_display_source(VGABASE, display_source);
		via_lvds2_set_display_source(VGABASE, display_source);
		break;
	default:
		break;
	}

	drm_dbg_kms(dev, "Exiting %s.\n", __func__);
}
