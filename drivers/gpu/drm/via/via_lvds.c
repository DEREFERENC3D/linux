/*
 * Copyright © 2016-2018 Kevin Brace.
 * Copyright 2012 James Simmons. All Rights Reserved.
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.
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

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/pci.h>

#include <asm/olpc.h>

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>

#include <uapi/linux/i2c.h>

#include "via_drv.h"

#define TD0 200
#define TD1 25
#define TD2 0
#define TD3 25

/* Non-I2C bus FP native screen resolution information table.*/
static struct via_lvds_info via_lvds_info_table[] = {
	{ 640,  480},
	{ 800,  600},
	{1024,  768},
	{1280,  768},
	{1280, 1024},
	{1400, 1050},
	{1600, 1200},
	{1280,  800},
	{ 800,  480},
	{1024,  768},
	{1366,  768},
	{1024,  768},
	{1280,  768},
	{1280, 1024},
	{1400, 1050},
	{1600, 1200}
};

/* Power sequence relations */
struct td_timer {
	struct vga_regset tdRegs[2];
};

static struct td_timer td_timer_regs[] = {
	/* td_timer0 */
	{ { { VGA_CRT_IC, 0x8B, 0, 7 }, { VGA_CRT_IC, 0x8F, 0, 3 } } },
	/* td_timer1 */
	{ { { VGA_CRT_IC, 0x8C, 0, 7 }, { VGA_CRT_IC, 0x8F, 4, 7 } } },
	/* td_timer2 */
	{ { { VGA_CRT_IC, 0x8D, 0, 7 }, { VGA_CRT_IC, 0x90, 0, 3 } } },
	/* td_timer3 */
	{ { { VGA_CRT_IC, 0x8E, 0, 7 }, { VGA_CRT_IC, 0x90, 4, 7 } } }
};

/*
 * Function Name:  via_init_td_timing_regs
 * Description: Init TD timing register (power sequence)
 */
static void via_init_td_timing_regs(struct drm_device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	unsigned int td_timer[4] = { 500, 50, 0, 510 }, i;
	struct vga_registers timings;
	u32 reg_value;

	/* Fill primary power sequence */
	for (i = 0; i < 4; i++) {
		/* Calculate TD Timer, every step is 572.1uSec */
		reg_value = td_timer[i] * 10000 / 5721;

		timings.count = ARRAY_SIZE(td_timer_regs[i].tdRegs);
		timings.regs = td_timer_regs[i].tdRegs;
		load_value_to_registers(VGABASE, &timings, reg_value);
	}

	/* Note: VT3353 have two hardware power sequences
	 * other chips only have one hardware power sequence */
	if (pdev->device == PCI_DEVICE_ID_VIA_CHROME9_HC3) {
		/* set CRD4[0] to "1" to select 2nd LCD power sequence. */
		svga_wcrt_mask(VGABASE, 0xD4, BIT(0), BIT(0));
		/* Fill secondary power sequence */
		for (i = 0; i < 4; i++) {
			/* Calculate TD Timer, every step is 572.1uSec */
			reg_value = td_timer[i] * 10000 / 5721;

			timings.count = ARRAY_SIZE(td_timer_regs[i].tdRegs);
			timings.regs = td_timer_regs[i].tdRegs;
			load_value_to_registers(VGABASE, &timings, reg_value);
		}
	}
}

static bool via_fp_probe_edid(struct i2c_adapter *i2c_bus)
{
	u8 out = 0x0;
	u8 buf[8];
	struct i2c_msg msgs[] = {
		{
			.addr = DDC_ADDR,
			.flags = 0,
			.len = 1,
			.buf = &out,
		},
		{
			.addr = DDC_ADDR,
			.flags = I2C_M_RD,
			.len = 8,
			.buf = buf,
		}
	};
	int i2c_ret;
	bool ret = false;

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	i2c_ret = i2c_transfer(i2c_bus, msgs, 2);
	if (i2c_ret != 2) {
		goto exit;
	}

	if (drm_edid_header_is_valid(buf) < 6) {
		goto exit;
	}

	ret = true;
exit:
	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
	return ret;
}

static void via_lvds_cle266_soft_power_seq(struct via_drm_priv *dev_priv,
						bool power_state)
{
	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	if (power_state) {
		/* Wait for 25 ms. */
		mdelay(25);

		/* Turn on FP VDD rail. */
		via_lvds_set_primary_soft_vdd(VGABASE, true);

		/* Wait for 510 ms. */
		mdelay(510);

		/* Turn on FP data transmission. */
		via_lvds_set_primary_soft_data(VGABASE, true);

		/* Wait for 1 ms. */
		mdelay(1);

		/* Turn on FP VEE rail. */
		via_lvds_set_primary_soft_vee(VGABASE, true);

		/* Turn on FP back light. */
		via_lvds_set_primary_soft_back_light(VGABASE, true);
	} else {
		/* Wait for 1 ms. */
		mdelay(1);

		/* Turn off FP back light. */
		via_lvds_set_primary_soft_back_light(VGABASE, false);

		/* Turn off FP VEE rail. */
		via_lvds_set_primary_soft_vee(VGABASE, false);

		/* Wait for 510 ms. */
		mdelay(510);

		/* Turn off FP data transmission. */
		via_lvds_set_primary_soft_data(VGABASE, false);

		/* Wait for 25 ms. */
		mdelay(25);

		/* Turn off FP VDD rail. */
		via_lvds_set_primary_soft_vdd(VGABASE, false);
	}

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

static void via_lvds_primary_soft_power_seq(struct via_drm_priv *dev_priv,
						bool power_state)
{
	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	/* Turn off FP hardware power sequence. */
	via_lvds_set_primary_hard_power(VGABASE, false);

	/* Use software FP power sequence control. */
	via_lvds_set_primary_power_seq_type(VGABASE, false);

	if (power_state) {
		/* Turn on FP display period. */
		via_lvds_set_primary_direct_display_period(VGABASE, true);

		/* Wait for TD0 ms. */
		mdelay(TD0);

		/* Turn on FP VDD rail. */
		via_lvds_set_primary_soft_vdd(VGABASE, true);

		/* Wait for TD1 ms. */
		mdelay(TD1);

		/* Turn on FP data transmission. */
		via_lvds_set_primary_soft_data(VGABASE, true);

		/* Wait for TD2 ms. */
		mdelay(TD2);

		/* Turn on FP VEE rail. */
		via_lvds_set_primary_soft_vee(VGABASE, true);

		/* Wait for TD3 ms. */
		mdelay(TD3);

		/* Turn on FP back light. */
		via_lvds_set_primary_soft_back_light(VGABASE, true);
	} else {
		/* Turn off FP back light. */
		via_lvds_set_primary_soft_back_light(VGABASE, false);

		/* Wait for TD3 ms. */
		mdelay(TD3);

		/* Turn off FP VEE rail. */
		via_lvds_set_primary_soft_vee(VGABASE, false);

		/* Wait for TD2 ms. */
		mdelay(TD2);

		/* Turn off FP data transmission. */
		via_lvds_set_primary_soft_data(VGABASE, false);

		/* Wait for TD1 ms. */
		mdelay(TD1);

		/* Turn off FP VDD rail. */
		via_lvds_set_primary_soft_vdd(VGABASE, false);

		/* Turn off FP display period. */
		via_lvds_set_primary_direct_display_period(VGABASE, false);
	}

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

static void via_lvds_secondary_soft_power_seq(struct via_drm_priv *dev_priv,
						bool power_state)
{
	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	/* Turn off FP hardware power sequence. */
	via_lvds_set_secondary_hard_power(VGABASE, false);

	/* Use software FP power sequence control. */
	via_lvds_set_secondary_power_seq_type(VGABASE, false);

	if (power_state) {
		/* Turn on FP display period. */
		via_lvds_set_secondary_direct_display_period(VGABASE, true);

		/* Wait for TD0 ms. */
		mdelay(TD0);

		/* Turn on FP VDD rail. */
		via_lvds_set_secondary_soft_vdd(VGABASE, true);

		/* Wait for TD1 ms. */
		mdelay(TD1);

		/* Turn on FP data transmission. */
		via_lvds_set_secondary_soft_data(VGABASE, true);

		/* Wait for TD2 ms. */
		mdelay(TD2);

		/* Turn on FP VEE rail. */
		via_lvds_set_secondary_soft_vee(VGABASE, true);

		/* Wait for TD3 ms. */
		mdelay(TD3);

		/* Turn on FP back light. */
		via_lvds_set_secondary_soft_back_light(VGABASE, true);
	} else {
		/* Turn off FP back light. */
		via_lvds_set_secondary_soft_back_light(VGABASE, false);

		/* Wait for TD3 ms. */
		mdelay(TD3);

		/* Turn off FP VEE rail. */
		via_lvds_set_secondary_soft_vee(VGABASE, false);

		/* Wait for TD2 ms. */
		mdelay(TD2);

		/* Turn off FP data transmission. */
		via_lvds_set_secondary_soft_data(VGABASE, false);

		/* Wait for TD1 ms. */
		mdelay(TD1);

		/* Turn off FP VDD rail. */
		via_lvds_set_secondary_soft_vdd(VGABASE, false);

		/* Turn off FP display period. */
		via_lvds_set_secondary_direct_display_period(VGABASE, false);
	}

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

static void via_lvds_primary_hard_power_seq(struct via_drm_priv *dev_priv,
						bool power_state)
{
	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	/* Use hardware FP power sequence control. */
	via_lvds_set_primary_power_seq_type(VGABASE, true);

	if (power_state) {
		/* Turn on FP display period. */
		via_lvds_set_primary_direct_display_period(VGABASE, true);

		/* Turn on FP hardware power sequence. */
		via_lvds_set_primary_hard_power(VGABASE, true);

		/* Turn on FP back light. */
		via_lvds_set_primary_direct_back_light_ctrl(VGABASE, true);
	} else {
		/* Turn off FP back light. */
		via_lvds_set_primary_direct_back_light_ctrl(VGABASE, false);

		/* Turn off FP hardware power sequence. */
		via_lvds_set_primary_hard_power(VGABASE, false);

		/* Turn on FP display period. */
		via_lvds_set_primary_direct_display_period(VGABASE, false);
	}

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

static void via_lvds_power(struct via_drm_priv *dev_priv,
				unsigned short device,
				u32 di_port, bool power_state)
{
	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	switch (device) {
	case PCI_DEVICE_ID_VIA_CLE266_GFX:
		via_lvds_cle266_soft_power_seq(dev_priv, power_state);
		break;
	case PCI_DEVICE_ID_VIA_KM400_GFX:
	case PCI_DEVICE_ID_VIA_P4M800_PRO_GFX:
	case PCI_DEVICE_ID_VIA_PM800_GFX:
	case PCI_DEVICE_ID_VIA_K8M800_GFX:
	case PCI_DEVICE_ID_VIA_P4M890_GFX:
	case PCI_DEVICE_ID_VIA_CHROME9:
	case PCI_DEVICE_ID_VIA_CHROME9_HC:
		via_lvds_primary_hard_power_seq(dev_priv, power_state);
		break;
	case PCI_DEVICE_ID_VIA_UNICHROME_PRO_II:
	case PCI_DEVICE_ID_VIA_CHROME9_HC3:
		if (di_port & VIA_DI_PORT_LVDS1) {
			via_lvds_primary_soft_power_seq(dev_priv, power_state);
			via_lvds1_set_power(VGABASE, power_state);
		}

		if (di_port & VIA_DI_PORT_LVDS2) {
			via_lvds_secondary_soft_power_seq(dev_priv, power_state);
			via_lvds2_set_power(VGABASE, power_state);
		}

		break;
	case PCI_DEVICE_ID_VIA_CHROME9_HCM:
	case PCI_DEVICE_ID_VIA_CHROME9_HD:
		via_lvds_primary_hard_power_seq(dev_priv, power_state);
		via_lvds1_set_power(VGABASE, power_state);
		break;
	default:
		DRM_DEBUG_KMS("VIA Technologies Chrome IGP "
				"FP Power: Unrecognized "
				"PCI Device ID.\n");
		break;
	}

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

/*
 * Sets flat panel I/O pad state.
 */
static void via_lvds_io_pad_setting(struct via_drm_priv *dev_priv,
					u32 di_port, bool io_pad_on)
{
	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	switch(di_port) {
	case VIA_DI_PORT_DVP0:
		via_dvp0_set_io_pad_state(VGABASE, io_pad_on ? 0x03 : 0x00);
		break;
	case VIA_DI_PORT_DVP1:
		via_dvp1_set_io_pad_state(VGABASE, io_pad_on ? 0x03 : 0x00);
		break;
	case VIA_DI_PORT_FPDPLOW:
		via_fpdp_low_set_io_pad_state(VGABASE, io_pad_on ? 0x03 : 0x00);
		break;
	case VIA_DI_PORT_FPDPHIGH:
		via_fpdp_high_set_io_pad_state(VGABASE, io_pad_on ? 0x03 : 0x00);
		break;
	case (VIA_DI_PORT_FPDPLOW |
		VIA_DI_PORT_FPDPHIGH):
		via_fpdp_low_set_io_pad_state(VGABASE, io_pad_on ? 0x03 : 0x00);
		via_fpdp_high_set_io_pad_state(VGABASE, io_pad_on ? 0x03 : 0x00);
		break;
	case VIA_DI_PORT_LVDS1:
		via_lvds1_set_io_pad_setting(VGABASE, io_pad_on ? 0x03 : 0x00);
		break;
	case VIA_DI_PORT_LVDS2:
		via_lvds2_set_io_pad_setting(VGABASE, io_pad_on ? 0x03 : 0x00);
		break;
	case (VIA_DI_PORT_LVDS1 |
		VIA_DI_PORT_LVDS2):
		via_lvds1_set_io_pad_setting(VGABASE, io_pad_on ? 0x03 : 0x00);
		via_lvds2_set_io_pad_setting(VGABASE, io_pad_on ? 0x03 : 0x00);
		break;
	default:
		break;
	}

	DRM_DEBUG_KMS("FP I/O Pad: %s\n", io_pad_on ? "On": "Off");

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

static void via_lvds_format(struct via_drm_priv *dev_priv,
				u32 di_port, u8 format)
{
	u8 temp = format & 0x01;

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	switch(di_port) {
	case VIA_DI_PORT_LVDS1:
		via_lvds1_set_format(VGABASE, temp);
		break;
	case VIA_DI_PORT_LVDS2:
		via_lvds2_set_format(VGABASE, temp);
		break;
	case (VIA_DI_PORT_LVDS1 |
		VIA_DI_PORT_LVDS2):
		via_lvds1_set_format(VGABASE, temp);
		via_lvds2_set_format(VGABASE, temp);
		break;
	default:
		break;
	}

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

static void via_lvds_output_format(struct via_drm_priv *dev_priv,
					u32 di_port, u8 output_format)
{
	u8 temp = output_format & 0x01;

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	switch(di_port) {
	case VIA_DI_PORT_LVDS1:
		via_lvds1_set_output_format(VGABASE, temp);
		break;
	case VIA_DI_PORT_LVDS2:
		via_lvds2_set_output_format(VGABASE, temp);
		break;
	case (VIA_DI_PORT_LVDS1 |
		VIA_DI_PORT_LVDS2):
		via_lvds1_set_output_format(VGABASE, temp);
		via_lvds2_set_output_format(VGABASE, temp);
		break;
	default:
		break;
	}

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

static void via_lvds_dithering(struct via_drm_priv *dev_priv,
				u32 di_port, bool dithering)
{
	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	switch(di_port) {
	case VIA_DI_PORT_LVDS1:
		via_lvds1_set_dithering(VGABASE, dithering);
		break;
	case VIA_DI_PORT_LVDS2:
		via_lvds2_set_dithering(VGABASE, dithering);
		break;
	case (VIA_DI_PORT_LVDS1 |
		VIA_DI_PORT_LVDS2):
		via_lvds1_set_dithering(VGABASE, dithering);
		via_lvds2_set_dithering(VGABASE, dithering);
		break;
	default:
		break;
	}

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

/*
 * Sets flat panel display source.
 */
static void via_lvds_display_source(struct via_drm_priv *dev_priv,
					u32 di_port, int index)
{
	u8 display_source = index & 0x01;

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	switch(di_port) {
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

	DRM_DEBUG_KMS("FP Display Source: IGA%d\n",
			display_source + 1);

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

static void via_lvds_dpms(struct drm_encoder *encoder, int mode)
{
	struct via_encoder *enc = container_of(encoder,
					struct via_encoder, base);
	struct drm_device *dev = encoder->dev;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		via_lvds_power(dev_priv, pdev->device, enc->di_port,
				true);
		via_lvds_io_pad_setting(dev_priv, enc->di_port, true);
		break;
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_OFF:
		via_lvds_power(dev_priv, pdev->device, enc->di_port,
				false);
		via_lvds_io_pad_setting(dev_priv, enc->di_port, false);
		break;
	default:
		break;
	}

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

static void via_lvds_prepare(struct drm_encoder *encoder)
{
	struct via_encoder *enc = container_of(encoder,
					struct via_encoder, base);
	struct drm_device *dev = encoder->dev;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	via_lvds_power(dev_priv, pdev->device, enc->di_port, false);
	via_lvds_io_pad_setting(dev_priv, enc->di_port, false);

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

static void via_lvds_commit(struct drm_encoder *encoder)
{
	struct via_encoder *enc = container_of(encoder,
					struct via_encoder, base);
	struct drm_device *dev = encoder->dev;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	via_lvds_power(dev_priv, pdev->device, enc->di_port, true);
	via_lvds_io_pad_setting(dev_priv, enc->di_port, true);

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

static void
via_lvds_mode_set(struct drm_encoder *encoder, struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	struct via_crtc *iga = container_of(encoder->crtc, struct via_crtc, base);
	struct via_encoder *enc = container_of(encoder, struct via_encoder, base);
	struct drm_device *dev = encoder->dev;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	/* Temporary implementation.*/
	switch (pdev->device) {
	case PCI_DEVICE_ID_VIA_CHROME9_HC:
		via_fpdp_low_set_adjustment(VGABASE, 0x08);
		break;
	default:
		break;
	}

	switch (pdev->device) {
	case PCI_DEVICE_ID_VIA_UNICHROME_PRO_II:
	case PCI_DEVICE_ID_VIA_CHROME9_HC3:
	case PCI_DEVICE_ID_VIA_CHROME9_HCM:
	case PCI_DEVICE_ID_VIA_CHROME9_HD:
		/* OPENLDI Mode */
		via_lvds_format(dev_priv, enc->di_port, 0x01);

		/* Sequential Mode */
		via_lvds_output_format(dev_priv, enc->di_port, 0x01);

		/* Turn on dithering. */
		via_lvds_dithering(dev_priv, enc->di_port, true);
		break;
	default:
		break;
	}

	via_lvds_display_source(dev_priv, enc->di_port, iga->index);

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

static void via_lvds_disable(struct drm_encoder *encoder)
{
	struct via_encoder *enc = container_of(encoder,
					struct via_encoder, base);
	struct drm_device *dev = encoder->dev;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	via_lvds_power(dev_priv, pdev->device, enc->di_port, false);
	via_lvds_io_pad_setting(dev_priv, enc->di_port, false);

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

const struct drm_encoder_helper_funcs via_lvds_helper_funcs = {
	.dpms = via_lvds_dpms,
	.prepare = via_lvds_prepare,
	.commit = via_lvds_commit,
	.mode_set = via_lvds_mode_set,
	.disable = via_lvds_disable,
};

const struct drm_encoder_funcs via_lvds_enc_funcs = {
	.destroy = via_encoder_destroy,
};

/* Detect FP presence. */
static enum drm_connector_status
via_lvds_detect(struct drm_connector *connector, bool force)
{
	struct drm_device *dev = connector->dev;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct via_connector *con = container_of(connector,
					struct via_connector, base);
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	enum drm_connector_status ret = connector_status_disconnected;
	struct i2c_adapter *i2c_bus;
	struct edid *edid = NULL;
	u8 mask;
	uint32_t i, i2c_bus_bit;

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	if (machine_is_olpc()) {
		ret = connector_status_connected;
		goto exit;
	}

	i2c_bus_bit = VIA_I2C_BUS2;
	for (i = 0; i < 2; i++) {
		if (con->i2c_bus & i2c_bus_bit) {
			if (i2c_bus_bit & VIA_I2C_BUS2) {
				i2c_bus = via_find_ddc_bus(0x31);
			} else if (i2c_bus_bit & VIA_I2C_BUS3) {
				i2c_bus = via_find_ddc_bus(0x2c);
			} else {
				i2c_bus = NULL;
				i2c_bus_bit = i2c_bus_bit << 1;
				continue;
			}
		} else {
			i2c_bus = NULL;
			i2c_bus_bit = i2c_bus_bit << 1;
			continue;
		}

		if (!via_fp_probe_edid(i2c_bus)) {
			i2c_bus_bit = i2c_bus_bit << 1;
			continue;
		}

		edid = drm_get_edid(&con->base, i2c_bus);
		if (edid) {
			if (edid->input & DRM_EDID_INPUT_DIGITAL) {
				ret = connector_status_connected;
				kfree(edid);
				DRM_DEBUG_KMS("FP detected.\n");
				DRM_DEBUG_KMS("i2c_bus_bit: %x\n", i2c_bus_bit);
				goto exit;
			} else {
				kfree(edid);
			}
		}

		i2c_bus_bit = i2c_bus_bit << 1;
	}

	if (pdev->device == PCI_DEVICE_ID_VIA_CLE266_GFX) {
		mask = BIT(3);
	} else {
		mask = BIT(1);
	}

	if (vga_rcrt(VGABASE, 0x3B) & mask) {
		ret = connector_status_connected;
	}

exit:
	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
	return ret;
}

struct drm_connector_funcs via_lvds_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = via_lvds_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = via_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state =
			drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state =
			drm_atomic_helper_connector_destroy_state,
};

static int
via_lvds_get_modes(struct drm_connector *connector)
{
	struct via_connector *con = container_of(connector, struct via_connector, base);
	struct drm_device *dev = connector->dev;
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	struct i2c_adapter *i2c_bus;
	struct edid *edid = NULL;
	struct drm_display_mode *native_mode = NULL;
	u8 reg_value;
	int hdisplay, vdisplay;
	int count = 0;
	uint32_t i, i2c_bus_bit;

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	/* OLPC is very special */
	if (machine_is_olpc()) {
		native_mode = drm_mode_create(dev);

		native_mode->clock = 56519;
		native_mode->hdisplay = 1200;
		native_mode->hsync_start = 1211;
		native_mode->hsync_end = 1243;
		native_mode->htotal = 1264;
		native_mode->hskew = 0;
		native_mode->vdisplay = 900;
		native_mode->vsync_start = 901;
		native_mode->vsync_end = 911;
		native_mode->vtotal = 912;
		native_mode->vscan = 0;

		native_mode->type = DRM_MODE_TYPE_PREFERRED |
					DRM_MODE_TYPE_DRIVER;
		drm_mode_set_name(native_mode);
		drm_mode_probed_add(connector, native_mode);
		count = 1;
		goto exit;
	}

	i2c_bus_bit = VIA_I2C_BUS2;
	for (i = 0; i < 2; i++) {
		if (con->i2c_bus & i2c_bus_bit) {
			if (i2c_bus_bit & VIA_I2C_BUS2) {
				i2c_bus = via_find_ddc_bus(0x31);
			} else if (i2c_bus_bit & VIA_I2C_BUS3) {
				i2c_bus = via_find_ddc_bus(0x2c);
			} else {
				i2c_bus = NULL;
				i2c_bus_bit = i2c_bus_bit << 1;
				continue;
			}
		} else {
			i2c_bus = NULL;
			i2c_bus_bit = i2c_bus_bit << 1;
			continue;
		}

		edid = drm_get_edid(&con->base, i2c_bus);
		if (edid) {
			if (edid->input & DRM_EDID_INPUT_DIGITAL) {
				drm_connector_update_edid_property(connector, edid);
				count = drm_add_edid_modes(connector, edid);
				kfree(edid);
				DRM_DEBUG_KMS("FP EDID information was obtained.\n");
				DRM_DEBUG_KMS("i2c_bus_bit: %x\n", i2c_bus_bit);
				break;
			} else {
				kfree(edid);
			}
		}

		i2c_bus_bit = i2c_bus_bit << 1;
	}

	reg_value = (vga_rcrt(VGABASE, 0x3f) & 0x0f);
	hdisplay = vdisplay = 0;
	hdisplay = via_lvds_info_table[reg_value].x;
	vdisplay = via_lvds_info_table[reg_value].y;

	if (hdisplay && vdisplay) {
		native_mode = drm_cvt_mode(dev, hdisplay, vdisplay,
					60, false, false, false);
	}

	if (native_mode) {
		native_mode->type = DRM_MODE_TYPE_PREFERRED |
						DRM_MODE_TYPE_DRIVER;
		drm_mode_set_name(native_mode);
		drm_mode_probed_add(connector, native_mode);
		count = 1;
	}

exit:
	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
	return count;
}

struct drm_connector_helper_funcs via_lvds_connector_helper_funcs = {
	.get_modes = via_lvds_get_modes,
};

/*
 * Probe (pre-initialization detection) FP.
 */
void via_lvds_probe(struct drm_device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	struct drm_connector connector;
	struct i2c_adapter *i2c_bus;
	struct edid *edid;
	u8 sr12, sr13, sr5a;
	u8 cr3b;

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	sr12 = vga_rseq(VGABASE, 0x12);
	sr13 = vga_rseq(VGABASE, 0x13);
	cr3b = vga_rcrt(VGABASE, 0x3b);

	DRM_DEBUG_KMS("sr12: 0x%02x\n", sr12);
	DRM_DEBUG_KMS("sr13: 0x%02x\n", sr13);
	DRM_DEBUG_KMS("cr3b: 0x%02x\n", cr3b);

	/* Detect the presence of FPs. */
	switch (pdev->device) {
	case PCI_DEVICE_ID_VIA_CLE266_GFX:
		/*
		 * 3C5.12[4] - FPD17 pin strapping (DIP1)
		 *             0: DVI / Capture
		 *             1: Panel
		 */
		if ((sr12 & BIT(4)) || (cr3b & BIT(3))) {
			dev_priv->int_fp1_presence = true;
			dev_priv->int_fp1_di_port = VIA_DI_PORT_DIP1;
		} else {
			dev_priv->int_fp1_presence = false;
			dev_priv->int_fp1_di_port = VIA_DI_PORT_NONE;
		}

		dev_priv->int_fp2_presence = false;
		dev_priv->int_fp2_di_port = VIA_DI_PORT_NONE;
		break;
	case PCI_DEVICE_ID_VIA_KM400_GFX:
	case PCI_DEVICE_ID_VIA_P4M800_PRO_GFX:
	case PCI_DEVICE_ID_VIA_PM800_GFX:
	case PCI_DEVICE_ID_VIA_K8M800_GFX:
		/* 3C5.13[3] - DVP0D8 pin strapping
		 *             0: AGP pins are used for AGP
		 *             1: AGP pins are used by FPDP
		 *             (Flat Panel Display Port) */
		if ((sr13 & BIT(3)) && (cr3b & BIT(1))) {
			dev_priv->int_fp1_presence = true;
			dev_priv->int_fp1_di_port = VIA_DI_PORT_FPDPHIGH |
							VIA_DI_PORT_FPDPLOW;
		} else {
			dev_priv->int_fp1_presence = false;
			dev_priv->int_fp1_di_port = VIA_DI_PORT_NONE;
		}

		dev_priv->int_fp2_presence = false;
		dev_priv->int_fp2_di_port = VIA_DI_PORT_NONE;
		break;
	case PCI_DEVICE_ID_VIA_P4M890_GFX:
	case PCI_DEVICE_ID_VIA_CHROME9:
	case PCI_DEVICE_ID_VIA_CHROME9_HC:
		if (cr3b & BIT(1)) {
			/* 3C5.12[4] - DVP0D4 pin strapping
			 *             0: 12-bit FPDP (Flat Panel
			 *                Display Port)
			 *             1: 24-bit FPDP (Flat Panel
			 *                Display Port) */
			if (sr12 & BIT(4)) {
				dev_priv->int_fp1_presence = true;
				dev_priv->int_fp1_di_port = VIA_DI_PORT_FPDPLOW |
							VIA_DI_PORT_FPDPHIGH;
			} else {
				dev_priv->int_fp1_presence = true;
				dev_priv->int_fp1_di_port =
						VIA_DI_PORT_FPDPLOW;
			}
		} else {
			dev_priv->int_fp1_presence = false;
			dev_priv->int_fp1_di_port = VIA_DI_PORT_NONE;
		}

		dev_priv->int_fp2_presence = false;
		dev_priv->int_fp2_di_port = VIA_DI_PORT_NONE;
		break;
	case PCI_DEVICE_ID_VIA_UNICHROME_PRO_II:
	case PCI_DEVICE_ID_VIA_CHROME9_HC3:
	case PCI_DEVICE_ID_VIA_CHROME9_HCM:
	case PCI_DEVICE_ID_VIA_CHROME9_HD:
		/* Save SR5A. */
		sr5a = vga_rseq(VGABASE, 0x5a);

		DRM_DEBUG_KMS("sr5a: 0x%02x\n", sr5a);

		/* Set SR5A[0] to 1.
		 * This allows the read out of the alternative
		 * pin strapping settings from SR12 and SR13. */
		svga_wseq_mask(VGABASE, 0x5a, BIT(0), BIT(0));

		sr13 = vga_rseq(VGABASE, 0x13);
		if (cr3b & BIT(1)) {
			if (dev_priv->is_via_nanobook) {
				dev_priv->int_fp1_presence = false;
				dev_priv->int_fp1_di_port = VIA_DI_PORT_NONE;
				dev_priv->int_fp2_presence = true;
				dev_priv->int_fp2_di_port = VIA_DI_PORT_LVDS2;
			} else if (dev_priv->is_quanta_il1) {
				/* From the Quanta IL1 schematic. */
				dev_priv->int_fp1_presence = true;
				dev_priv->int_fp1_di_port = VIA_DI_PORT_DVP1;
				dev_priv->int_fp2_presence = false;
				dev_priv->int_fp2_di_port = VIA_DI_PORT_NONE;

			} else if (dev_priv->is_samsung_nc20) {
				dev_priv->int_fp1_presence = false;
				dev_priv->int_fp1_di_port = VIA_DI_PORT_NONE;
				dev_priv->int_fp2_presence = true;
				dev_priv->int_fp2_di_port = VIA_DI_PORT_LVDS2;

			/* 3C5.13[7:6] - Integrated LVDS / DVI
			 *               Mode Select (DVP1D15-14 pin
			 *               strapping)
			 *               00: LVDS1 + LVDS2
			 *               01: DVI + LVDS2
			 *               10: Dual LVDS Channel
			 *                   (High Resolution Panel)
			 *               11: One DVI only (decrease
			 *                   the clock jitter) */
			} else if ((!(sr13 & BIT(7))) &&
					(!(sr13 & BIT(6)))) {
				dev_priv->int_fp1_presence = true;
				dev_priv->int_fp1_di_port = VIA_DI_PORT_LVDS1;

				/*
				 * For now, don't support the second
				 * FP.
				 */
				dev_priv->int_fp2_presence = false;
				dev_priv->int_fp2_di_port = VIA_DI_PORT_NONE;
			} else if ((!(sr13 & BIT(7))) &&
					(sr13 & BIT(6))) {
				dev_priv->int_fp1_presence = false;
				dev_priv->int_fp1_di_port = VIA_DI_PORT_NONE;
				dev_priv->int_fp2_presence = true;
				dev_priv->int_fp2_di_port = VIA_DI_PORT_LVDS2;
			} else if ((sr13 & BIT(7)) &&
					(!(sr13 & BIT(6)))) {
				dev_priv->int_fp1_presence = true;
				dev_priv->int_fp1_di_port = VIA_DI_PORT_LVDS1 |
							VIA_DI_PORT_LVDS2;
				dev_priv->int_fp2_presence = false;
				dev_priv->int_fp2_di_port = VIA_DI_PORT_NONE;
			} else {
				dev_priv->int_fp1_presence = false;
				dev_priv->int_fp1_di_port = VIA_DI_PORT_NONE;
				dev_priv->int_fp2_presence = false;
				dev_priv->int_fp2_di_port = VIA_DI_PORT_NONE;
			}
		} else {
			dev_priv->int_fp1_presence = false;
			dev_priv->int_fp1_di_port = VIA_DI_PORT_NONE;
			dev_priv->int_fp2_presence = false;
			dev_priv->int_fp2_di_port = VIA_DI_PORT_NONE;
		}

		/* Restore SR5A. */
		vga_wseq(VGABASE, 0x5a, sr5a);
		break;
	default:
		dev_priv->int_fp1_presence = false;
		dev_priv->int_fp1_di_port = VIA_DI_PORT_NONE;
		dev_priv->int_fp2_presence = false;
		dev_priv->int_fp2_di_port = VIA_DI_PORT_NONE;
		break;
	}

	dev_priv->int_fp1_i2c_bus = VIA_I2C_NONE;
	dev_priv->int_fp2_i2c_bus = VIA_I2C_NONE;

	/* Zero clear connector struct.
	 * Not doing so leads to a crash. */
	memset(&connector, 0, sizeof(connector));

	/* Register a connector only for I2C bus probing. */
	drm_connector_init(dev, &connector, &via_lvds_connector_funcs,
				DRM_MODE_CONNECTOR_LVDS);
	drm_connector_helper_add(&connector,
					&via_lvds_connector_helper_funcs);
	drm_connector_register(&connector);

	if ((dev_priv->int_fp1_presence)
		&& (!(dev_priv->mapped_i2c_bus & VIA_I2C_BUS2))) {
		i2c_bus = via_find_ddc_bus(0x31);
		edid = drm_get_edid(&connector, i2c_bus);
		if (edid) {
			dev_priv->int_fp1_i2c_bus = VIA_I2C_BUS2;
			dev_priv->mapped_i2c_bus |= VIA_I2C_BUS2;
			kfree(edid);
		}
	}

	if ((dev_priv->int_fp2_presence)
		&& (!(dev_priv->mapped_i2c_bus & VIA_I2C_BUS2))) {
		i2c_bus = via_find_ddc_bus(0x31);
		edid = drm_get_edid(&connector, i2c_bus);
		if (edid) {
			dev_priv->int_fp2_i2c_bus = VIA_I2C_BUS2;
			dev_priv->mapped_i2c_bus |= VIA_I2C_BUS2;
			kfree(edid);
		}
	}

	/* Release the connector resource. */
	drm_connector_unregister(&connector);
	drm_connector_cleanup(&connector);

	DRM_DEBUG_KMS("int_fp1_presence: %x\n",
			dev_priv->int_fp1_presence);
	DRM_DEBUG_KMS("int_fp1_di_port: 0x%08x\n",
			dev_priv->int_fp1_di_port);
	DRM_DEBUG_KMS("int_fp1_i2c_bus: 0x%08x\n",
			dev_priv->int_fp1_i2c_bus);
	DRM_DEBUG_KMS("int_fp2_presence: %x\n",
			dev_priv->int_fp2_presence);
	DRM_DEBUG_KMS("int_fp2_di_port: 0x%08x\n",
			dev_priv->int_fp2_di_port);
	DRM_DEBUG_KMS("int_fp2_i2c_bus: 0x%08x\n",
			dev_priv->int_fp2_i2c_bus);
	DRM_DEBUG_KMS("mapped_i2c_bus: 0x%08x\n",
			dev_priv->mapped_i2c_bus);

	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
}

void via_lvds_init(struct drm_device *dev)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	struct via_connector *con;
	struct via_encoder *enc;

	DRM_DEBUG_KMS("Entered %s.\n", __func__);

	if ((!(dev_priv->int_fp1_presence)) &&
		(!(dev_priv->int_fp2_presence))) {
		goto exit;
	}

	enc = kzalloc(sizeof(*enc) + sizeof(*con), GFP_KERNEL);
	if (!enc) {
		DRM_ERROR("Failed to allocate FP.\n");
		goto exit;
	}

	con = &enc->cons[0];
	INIT_LIST_HEAD(&con->props);

	drm_connector_init(dev, &con->base, &via_lvds_connector_funcs,
				DRM_MODE_CONNECTOR_LVDS);
	drm_connector_helper_add(&con->base, &via_lvds_connector_helper_funcs);
	drm_connector_register(&con->base);

	if (dev_priv->int_fp1_presence) {
		con->i2c_bus = dev_priv->int_fp1_i2c_bus;
	} else if (dev_priv->int_fp2_presence) {
		con->i2c_bus = dev_priv->int_fp2_i2c_bus;
	} else {
		con->i2c_bus = VIA_I2C_NONE;
	}

	con->base.doublescan_allowed = false;
	con->base.interlace_allowed = false;

	/* Now setup the encoder */
	drm_encoder_init(dev, &enc->base, &via_lvds_enc_funcs,
						DRM_MODE_ENCODER_LVDS, NULL);
	drm_encoder_helper_add(&enc->base, &via_lvds_helper_funcs);

	enc->base.possible_crtcs = BIT(1) | BIT(0);

	if (dev_priv->int_fp1_presence) {
		enc->di_port = dev_priv->int_fp1_di_port;
	} else if (dev_priv->int_fp2_presence) {
		enc->di_port = dev_priv->int_fp2_di_port;
	} else {
		enc->di_port = VIA_DI_PORT_NONE;
	}

	/* Put it all together */
	drm_connector_attach_encoder(&con->base, &enc->base);

	/* Init TD timing register (power sequence) */
	via_init_td_timing_regs(dev);
exit:
	DRM_DEBUG_KMS("Exiting %s.\n", __func__);
	return;
}
