/*
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Author(s):
 * James Simmons <jsimmons@infradead.org>
 */

#ifndef __CRTC_HW_H__
#define __CRTC_HW_H__

#include <video/vga.h>

#include <drm/drm_print.h>


/* To be used with via_dac_set_dpms_control inline function. */
#define VIA_DAC_DPMS_ON		0x00
#define VIA_DAC_DPMS_STANDBY	0x01
#define VIA_DAC_DPMS_SUSPEND	0x02
#define VIA_DAC_DPMS_OFF	0x03


struct vga_regset {
	u16	ioport;
	u8	io_addr;
	u8	start_bit;
	u8	end_bit;
};

struct vga_registers {
	unsigned int count;
	struct vga_regset *regs;
};

/************************************************
 *****     Display Timing       *****
 ************************************************/

struct crtc_timings {
	struct vga_registers htotal;
	struct vga_registers hdisplay;
	struct vga_registers hblank_start;
	struct vga_registers hblank_end;
	struct vga_registers hsync_start;
	struct vga_registers hsync_end;
	struct vga_registers vtotal;
	struct vga_registers vdisplay;
	struct vga_registers vblank_start;
	struct vga_registers vblank_end;
	struct vga_registers vsync_start;
	struct vga_registers vsync_end;
};

/* Write a value to misc register with a mask */
static inline void svga_wmisc_mask(void __iomem *regbase, u8 data, u8 mask)
{
	vga_w(regbase, VGA_MIS_W, (data & mask) | (vga_r(regbase, VGA_MIS_R) & ~mask));
}

/* Write a value to a sequence register with a mask */
static inline void svga_wseq_mask(void __iomem *regbase, u8 index, u8 data, u8 mask)
{
	vga_wseq(regbase, index, (data & mask) | (vga_rseq(regbase, index) & ~mask));
}

/* Write a value to a CRT register with a mask */
static inline void svga_wcrt_mask(void __iomem *regbase, u8 index, u8 data, u8 mask)
{
	vga_wcrt(regbase, index, (data & mask) | (vga_rcrt(regbase, index) & ~mask));
}


/***********************************************************************

 VIA Technologies Chrome IGP Register Access Helper Functions

***********************************************************************/

static inline void
via_iga1_set_palette_lut_resolution(void __iomem *regs,
					bool palette_lut)
{
	/* Set the palette LUT resolution for IGA1. */
	/* 3C5.15[7] - IGA1 6 / 8 Bit LUT
	 *             0: 6-bit
	 *             1: 8-bit */
	svga_wseq_mask(regs, 0x15, palette_lut ? BIT(7) : 0x00, BIT(7));
}

static inline void
via_iga2_set_palette_lut_resolution(void __iomem *regs,
					bool palette_lut)
{
	/* Set the palette LUT resolution for IGA2. */
	/* 3X5.6A[5] - IGA2 6 / 8 Bit LUT
	 *             0: 6-bit
	 *             1: 8-bit */
	svga_wcrt_mask(regs, 0x6a, palette_lut ? BIT(5) : 0x00, BIT(5));
}

static inline void
via_iga1_set_interlace_mode(void __iomem *regs, bool interlace_mode)
{
	svga_wcrt_mask(regs, 0x33,
			interlace_mode ? BIT(6) : 0x00, BIT(6));
}

static inline void
via_iga2_set_interlace_mode(void __iomem *regs, bool interlace_mode)
{
	svga_wcrt_mask(regs, 0x67,
			interlace_mode ? BIT(5) : 0x00, BIT(5));
}

/*
 * Sets IGA1's HSYNC Shift value.
 */
static inline void
via_iga1_set_hsync_shift(void __iomem *regs, u8 shift_value)
{
	/* 3X5.33[2:0] - IGA1 HSYNC Shift */
	svga_wcrt_mask(regs, 0x33, shift_value, BIT(2) | BIT(1) | BIT(0));
}

/*
 * Sets DIP0 (Digital Interface Port 0) I/O pad state.
 * CLE266 chipset only.
 */
static inline void
via_dip0_set_io_pad_state(void __iomem *regs, u8 io_pad_state)
{
	/* 3C5.1E[7:6] - DIP0 Power Control
	 *               0x: Pad always off
	 *               10: Depend on the other control signal
	 *               11: Pad on/off according to the
	 *                   Power Management Status (PMS) */
	svga_wseq_mask(regs, 0x1E, io_pad_state << 6, BIT(7) | BIT(6));
}

/*
 * Output enable of DIP0 (Digital Interface Port 0) interface.
 * CLE266 chipset only.
 */
static inline void
via_dip0_set_output_enable(void __iomem *regs, bool output_enable)
{
	/*
	* 3X5.6C[0] - DIP0 Output Enable
	*             0: Output Disable
	*             1: Output Enable
	*/
	svga_wcrt_mask(regs, 0x6c, output_enable ? BIT(0) : 0x00, BIT(0));
}

/*
 * Sets the clock source of DIP0 (Digital Interface Port 0)
 * interface. CLE266 chipset only.
 */
static inline void
via_dip0_set_clock_source(void __iomem *regs, bool clock_source)
{
	/*
	 * 3X5.6C[5] - DIP0 Clock Source
	 *             0: External
	 *             1: Internal
	 */
	svga_wcrt_mask(regs, 0x6c, clock_source ? BIT(5) : 0x00, BIT(5));
}

/*
 * Sets the display source of DIP0 (Digital Interface Port 0) interface.
 * CLE266 chipset only.
 */
static inline void
via_dip0_set_display_source(void __iomem *regs, u8 display_source)
{
	/*
	 * 3X5.6C[7] - DIP0 Data Source Selection
	 *             0: Primary Display
	 *             1: Secondary Display
	 */
	svga_wcrt_mask(regs, 0x6c, display_source << 7, BIT(7));
}

/*
 * Sets DIP1 (Digital Interface Port 1) I/O pad state.
 * CLE266 chipset only.
 */
static inline void
via_dip1_set_io_pad_state(void __iomem *regs, u8 io_pad_state)
{
	/*
	 * 3C5.1E[5:4] - DIP1 I/O Pad Control
	 *               00: I/O pad off
	 *               11: I/O pad on
	 */
	svga_wseq_mask(regs, 0x1e, io_pad_state << 4, BIT(5) | BIT(4));
}

/*
 * Output enable of DIP1 (Digital Interface Port 1) interface.
 * CLE266 chipset only.
 */
static inline void
via_dip1_set_output_enable(void __iomem *regs, bool output_enable)
{
	/*
	 * 3X5.93[0] - DIP1 Output Enable
	 *             0: Output Disable
	 *             1: Output Enable
	 */
	svga_wcrt_mask(regs, 0x93, output_enable ? BIT(0) : 0x00, BIT(0));
}

/*
 * Sets the clock source of DIP1 (Digital Interface Port 1)
 * interface. CLE266 chipset only.
 */
static inline void
via_dip1_set_clock_source(void __iomem *regs, bool clock_source)
{
	/*
	 * 3X5.93[5] - DIP1 Clock Source
	 *             0: External
	 *             1: Internal
	 */
	svga_wcrt_mask(regs, 0x93, clock_source ? BIT(5) : 0x00, BIT(5));
}

/*
 * Sets the display source of DIP1 (Digital Interface Port 1)
 * interface. CLE266 chipset only.
 */
static inline void
via_dip1_set_display_source(void __iomem *regs, u8 display_source)
{
	/*
	 * 3X5.93[7] - DIP1 Data Source Selection
	 *             0: IGA1
	 *             1: IGA2
	 */
	svga_wcrt_mask(regs, 0x93, display_source << 7, BIT(7));
}

/*
 * Sets DVP0 (Digital Video Port 0) I/O pad state.
 */
static inline void
via_dvp0_set_io_pad_state(void __iomem *regs, u8 io_pad_state)
{
	/* 3C5.1E[7:6] - DVP0 Power Control
	 *               0x: Pad always off
	 *               10: Depend on the other control signal
	 *               11: Pad on/off according to the
	 *                   Power Management Status (PMS) */
	svga_wseq_mask(regs, 0x1E, io_pad_state << 6, BIT(7) | BIT(6));
}

/*
 * Sets DVP0 (Digital Video Port 0) clock I/O pad drive strength.
 */
static inline void
via_dvp0_set_clock_drive_strength(void __iomem *regs,
					u8 clock_drive_strength)
{
	/* 3C5.1E[2] - DVP0 Clock Drive Strength Bit [0] */
	svga_wseq_mask(regs, 0x1E,
			clock_drive_strength << 2, BIT(2));

	/* 3C5.2A[4] - DVP0 Clock Drive Strength Bit [1] */
	svga_wseq_mask(regs, 0x2A,
			clock_drive_strength << 3, BIT(4));
}

/*
 * Sets DVP0 (Digital Video Port 0) data I/O pads drive strength.
 */
static inline void
via_dvp0_set_data_drive_strength(void __iomem *regs,
					u8 data_drive_strength)
{
	/* 3C5.1B[1] - DVP0 Data Drive Strength Bit [0] */
	svga_wseq_mask(regs, 0x1B,
			data_drive_strength << 1, BIT(1));

	/* 3C5.2A[5] - DVP0 Data Drive Strength Bit [1] */
	svga_wseq_mask(regs, 0x2A,
			data_drive_strength << 4, BIT(5));
}

/*
 * Sets the display source of DVP0 (Digital Video Port 0) interface.
 */
static inline void
via_dvp0_set_display_source(void __iomem *regs, u8 display_source)
{
	/* 3X5.96[4] - DVP0 Data Source Selection
	 *             0: Primary Display
	 *             1: Secondary Display */
	svga_wcrt_mask(regs, 0x96, display_source << 4, BIT(4));
}

/*
 * Sets DVP1 (Digital Video Port 1) I/O pad state.
 */
static inline void
via_dvp1_set_io_pad_state(void __iomem *regs, u8 io_pad_state)
{
	/* 3C5.1E[5:4] - DVP1 Power Control
	 *               0x: Pad always off
	 *               10: Depend on the other control signal
	 *               11: Pad on/off according to the
	 *                   Power Management Status (PMS) */
	svga_wseq_mask(regs, 0x1E, io_pad_state << 4, BIT(5) | BIT(4));
}

/*
 * Sets DVP1 (Digital Video Port 1) clock I/O pad drive strength.
 */
static inline void
via_dvp1_set_clock_drive_strength(void __iomem *regs,
					u8 clock_drive_strength)
{
	/* 3C5.65[3:2] - DVP1 Clock Pads Driving Select [1:0]
	 *               00: lowest
	 *               01: low
	 *               10: high
	 *               11: highest */
	svga_wseq_mask(regs, 0x65,
			clock_drive_strength << 2, BIT(3) | BIT(2));
}

/*
 * Sets DVP1 (Digital Video Port 1) data I/O pads drive strength.
 */
static inline void
via_dvp1_set_data_drive_strength(void __iomem *regs,
					u8 data_drive_strength)
{
	/* 3C5.65[1:0] - DVP1 Data Pads Driving Select [1:0}
	 *               00: lowest
	 *               01: low
	 *               10: high
	 *               11: highest */
	svga_wseq_mask(regs, 0x65,
			data_drive_strength, BIT(1) | BIT(0));
}

/*
 * Sets the display source of DVP1 (Digital Video Port 1) interface.
 */
static inline void
via_dvp1_set_display_source(void __iomem *regs, u8 display_source)
{
	/* 3X5.9B[4] - DVP1 Data Source Selection
	 *             0: Primary Display
	 *             1: Secondary Display */
	svga_wcrt_mask(regs, 0x9B, display_source << 4, BIT(4));
}

/*
 * Sets analog (VGA) DAC power.
 */
static inline void via_dac_set_power(void __iomem *regs, bool output_state)
{
	/* 3X5.47[2] - DACOFF Backdoor Register
	 *             0: DAC on
	 *             1: DAC off */
	svga_wcrt_mask(regs, 0x47,
			output_state ? 0x00 : BIT(2), BIT(2));
}

/*
 * Sets analog (VGA) DPMS state.
 */
static inline void via_dac_set_dpms_control(void __iomem *regs,
						u8 dpms_control)
{
	/* 3X5.36[5:4] - DPMS Control
	 *               00: On
	 *               01: Stand-by
	 *               10: Suspend
	 *               11: Off */
	svga_wcrt_mask(regs, 0x36,
			dpms_control << 4, BIT(5) | BIT(4));
}

/*
 * Sets analog (VGA) sync polarity.
 */
static inline void via_dac_set_sync_polarity(void __iomem *regs,
						u8 sync_polarity)
{
	/* 3C2[7] - Analog Vertical Sync Polarity
	 *          0: Positive
	 *          1: Negative
	 * 3C2[6] - Analog Horizontal Sync Polarity
	 *          0: Positive
	 *          1: Negative */
	svga_wmisc_mask(regs,
			sync_polarity << 6, (BIT(1) | BIT(0)) << 6);
}

/*
 * Sets analog (VGA) display source.
 */
static inline void via_dac_set_display_source(void __iomem *regs,
						u8 display_source)
{
	/* 3C5.16[6] - CRT Display Source
	 *             0: Primary Display Stream (IGA1)
	 *             1: Secondary Display Stream (IGA2) */
	svga_wseq_mask(regs, 0x16,
			display_source << 6, BIT(6));
}

/*
 * Sets KM400 or later chipset's FP primary power sequence control
 * type.
 */
static inline void via_lvds_set_primary_power_seq_type(void __iomem *regs,
							bool ctrl_type)
{
	/* 3X5.91[0] - FP Primary Power Sequence Control Type
	 *             0: Hardware Control
	 *             1: Software Control */
	svga_wcrt_mask(regs, 0x91,
			ctrl_type ? 0x00 : BIT(0), BIT(0));
}

/*
 * Sets KM400 or later chipset's FP primary software controlled
 * back light.
 */
static inline void via_lvds_set_primary_soft_back_light(void __iomem *regs,
							bool soft_on)
{
	/* 3X5.91[1] - FP Primary Software Back Light On
	 *             0: Off
	 *             1: On */
	svga_wcrt_mask(regs, 0x91,
			soft_on ? BIT(1) : 0x00, BIT(1));
}

/*
 * Sets KM400 or later chipset's FP primary software controlled
 * VEE.
 */
static inline void via_lvds_set_primary_soft_vee(void __iomem *regs,
							bool soft_on)
{
	/* 3X5.91[2] - FP Primary Software VEE On
	 *             0: Off
	 *             1: On */
	svga_wcrt_mask(regs, 0x91,
			soft_on ? BIT(2) : 0x00, BIT(2));
}

/*
 * Sets KM400 or later chipset's FP primary software controlled
 * data.
 */
static inline void via_lvds_set_primary_soft_data(void __iomem *regs,
							bool soft_on)
{
	/* 3X5.91[3] - FP Primary Software Data On
	 *             0: Off
	 *             1: On */
	svga_wcrt_mask(regs, 0x91,
			soft_on ? BIT(3) : 0x00, BIT(3));
}

/*
 * Sets KM400 or later chipset's FP primary software controlled
 * VDD.
 */
static inline void via_lvds_set_primary_soft_vdd(void __iomem *regs,
							bool soft_on)
{
	/* 3X5.91[4] - FP Primary Software VDD On
	 *             0: Off
	 *             1: On */
	svga_wcrt_mask(regs, 0x91,
			soft_on ? BIT(4) : 0x00, BIT(4));
}

/*
 * Sets KM400 or later chipset's FP primary direct back
 * light control.
 */
static inline void via_lvds_set_primary_direct_back_light_ctrl(
					void __iomem *regs, bool direct_on)
{
	/* 3X5.91[6] - FP Primary Direct Back Light Control
	 *             0: On
	 *             1: Off */
	svga_wcrt_mask(regs, 0x91,
			direct_on ? 0x00 : BIT(6), BIT(6));
}

/*
 * Sets KM400 or later chipset's FP primary direct display
 * period control.
 */
static inline void via_lvds_set_primary_direct_display_period(
					void __iomem *regs, bool direct_on)
{
	/* 3X5.91[7] - FP Primary Direct Display Period Control
	 *             0: On
	 *             1: Off */
	svga_wcrt_mask(regs, 0x91,
			direct_on ? 0x00 : BIT(7), BIT(7));
}

/*
 * Sets KM400 or later chipset's FP primary hardware controlled
 * power sequence.
 */
static inline void via_lvds_set_primary_hard_power(void __iomem *regs,
							bool power_state)
{
	/* 3X5.6A[3] - FP Primary Hardware Controlled Power Sequence
	 *             0: Hardware Controlled Power Off
	 *             1: Hardware Controlled Power On */
	svga_wcrt_mask(regs, 0x6A,
			power_state ? BIT(3) : 0x00, BIT(3));
}

/*
 * Sets CX700 / VX700 or later chipset's FP secondary
 * power sequence control type.
 */
static inline void via_lvds_set_secondary_power_seq_type(void __iomem *regs,
							bool ctrl_type)
{
	/* 3X5.D3[0] - FP Secondary Power Sequence Control Type
	 *             0: Hardware Control
	 *             1: Software Control */
	svga_wcrt_mask(regs, 0xD3,
			ctrl_type ? 0x00 : BIT(0), BIT(0));
}

/*
 * Sets CX700 / VX700 or later chipset's FP secondary
 * software controlled back light.
 */
static inline void via_lvds_set_secondary_soft_back_light(void __iomem *regs,
								bool soft_on)
{
	/* 3X5.D3[1] - FP Secondary Software Back Light On
	 *             0: Off
	 *             1: On */
	svga_wcrt_mask(regs, 0xD3,
			soft_on ? BIT(1) : 0x00, BIT(1));
}

/*
 * Sets CX700 / VX700 or later chipset's FP secondary software
 * controlled VEE.
 */
static inline void via_lvds_set_secondary_soft_vee(void __iomem *regs,
							bool soft_on)
{
	/* 3X5.D3[2] - FP Secondary Software VEE On
	 *             0: Off
	 *             1: On */
	svga_wcrt_mask(regs, 0xD3,
			soft_on ? BIT(2) : 0x00, BIT(2));
}

/*
 * Sets CX700 / VX700 or later chipset's FP secondary software
 * controlled data.
 */
static inline void via_lvds_set_secondary_soft_data(void __iomem *regs,
							bool soft_on)
{
	/* 3X5.D3[3] - FP Secondary Software Data On
	 *             0: Off
	 *             1: On */
	svga_wcrt_mask(regs, 0xD3,
			soft_on ? BIT(3) : 0x00, BIT(3));
}

/*
 * Sets CX700 / VX700 or later chipset's FP secondary software
 * controlled VDD.
 */
static inline void via_lvds_set_secondary_soft_vdd(void __iomem *regs,
							bool soft_on)
{
	/* 3X5.D3[4] - FP Secondary Software VDD On
	 *             0: Off
	 *             1: On */
	svga_wcrt_mask(regs, 0xD3,
			soft_on ? BIT(4) : 0x00, BIT(4));
}

/*
 * Sets CX700 / VX700 or later chipset's FP secondary direct back
 * light control.
 */
static inline void via_lvds_set_secondary_direct_back_light_ctrl(
					void __iomem *regs, bool direct_on)
{
	/* 3X5.D3[6] - FP Secondary Direct Back Light Control
	 *             0: On
	 *             1: Off */
	svga_wcrt_mask(regs, 0xD3,
			direct_on ? 0x00 : BIT(6), BIT(6));
}

/*
 * Sets CX700 / VX700 or later chipset's FP secondary direct
 * display period control.
 */
static inline void via_lvds_set_secondary_direct_display_period(
					void __iomem *regs, bool direct_on)
{
	/* 3X5.D3[7] - FP Secondary Direct Display Period Control
	 *             0: On
	 *             1: Off */
	svga_wcrt_mask(regs, 0xD3,
			direct_on ? 0x00 : BIT(7), BIT(7));
}

/*
 * Sets FP secondary hardware controlled power sequence enable.
 */
static inline void via_lvds_set_secondary_hard_power(void __iomem *regs,
							bool power_state)
{
	/* 3X5.D4[1] - Secondary Power Hardware Power Sequence Enable
	 *             0: Off
	 *             1: On */
	svga_wcrt_mask(regs, 0xD4,
			power_state ? BIT(1) : 0x00, BIT(1));
}

/*
 * Sets FPDP (Flat Panel Display Port) Low I/O pad state.
 */
static inline void
via_fpdp_low_set_io_pad_state(void __iomem *regs, u8 io_pad_state)
{
	/* 3C5.2A[1:0] - FPDP Low I/O Pad Control
	 *               0x: Pad always off
	 *               10: Depend on the other control signal
	 *               11: Pad on/off according to the
	 *                   Power Management Status (PMS) */
	svga_wseq_mask(regs, 0x2A,
			io_pad_state, BIT(1) | BIT(0));
}

/*
 * Sets FPDP (Flat Panel Display Port) Low adjustment register.
 */
static inline void
via_fpdp_low_set_adjustment(void __iomem *regs, u8 adjustment)
{
	/* 3X5.99[3:0] - FPDP Low Adjustment */
	svga_wcrt_mask(regs, 0x99,
			adjustment, BIT(3) | BIT(2) | BIT(1) | BIT(0));
}

/*
 * Sets FPDP (Flat Panel Display Port) Low interface display source.
 */
static inline void
via_fpdp_low_set_display_source(void __iomem *regs, u8 display_source)
{
	/* 3X5.99[4] - FPDP Low Data Source Selection
	 *             0: Primary Display
	 *             1: Secondary Display */
	svga_wcrt_mask(regs, 0x99,
			display_source << 4, BIT(4));
}

/*
 * Sets FPDP (Flat Panel Display Port) High I/O pad state.
 */
static inline void
via_fpdp_high_set_io_pad_state(void __iomem *regs, u8 io_pad_state)
{
	/* 3C5.2A[3:2] - FPDP High I/O Pad Control
	 *               0x: Pad always off
	 *               10: Depend on the other control signal
	 *               11: Pad on/off according to the
	 *                   Power Management Status (PMS) */
	svga_wseq_mask(regs, 0x2A,
			io_pad_state << 2, BIT(3) | BIT(2));
}

/*
 * Sets FPDP (Flat Panel Display Port) High adjustment register.
 */
static inline void
via_fpdp_high_set_adjustment(void __iomem *regs, u8 adjustment)
{
	/* 3X5.97[3:0] - FPDP High Adjustment */
	svga_wcrt_mask(regs, 0x97,
			adjustment, BIT(3) | BIT(2) | BIT(1) | BIT(0));
}

/*
 * Sets FPDP (Flat Panel Display Port) High interface display source.
 */
static inline void
via_fpdp_high_set_display_source(void __iomem *regs, u8 display_source)
{
	/* 3X5.97[4] - FPDP High Data Source Selection
	 *             0: Primary Display
	 *             1: Secondary Display */
	svga_wcrt_mask(regs, 0x97,
			display_source << 4, BIT(4));
}

/*
 * Sets CX700 / VX700 or later chipset's LVDS1 power state.
 */
static inline void
via_lvds1_set_power(void __iomem *regs, bool power_state)
{
	/* 3X5.D2[7] - Power Down (Active High) for Channel 1 LVDS
	 *             0: Power on
	 *             1: Power off */
	svga_wcrt_mask(regs, 0xD2,
			power_state ? 0x00 : BIT(7), BIT(7));
}

/*
 * Sets CX700 or later single chipset's LVDS1 power sequence type.
 */
static inline void
via_lvds1_set_power_seq(void __iomem *regs, bool softCtrl)
{
	/* Set LVDS1 power sequence type. */
	/* 3X5.91[0] - LVDS1 Hardware or Software Control Power Sequence
	 *             0: Hardware Control
	 *             1: Software Control */
	svga_wcrt_mask(regs, 0x91, softCtrl ? BIT(0) : 0, BIT(0));
}

/*
 * Sets CX700 or later single chipset's LVDS1 software controlled
 * data path state.
 */
static inline void
via_lvds1_set_soft_data(void __iomem *regs, bool softOn)
{
	/* Set LVDS1 software controlled data path state. */
	/* 3X5.91[3] - Software Data On
	 *             0: Off
	 *             1: On */
	svga_wcrt_mask(regs, 0x91, softOn ? BIT(3) : 0, BIT(3));
}

/*
 * Sets CX700 or later single chipset's LVDS1 software controlled Vdd.
 */
static inline void
via_lvds1_set_soft_vdd(void __iomem *regs, bool softOn)
{
	/* Set LVDS1 software controlled Vdd. */
	/* 3X5.91[4] - Software VDD On
	 *             0: Off
	 *             1: On */
	svga_wcrt_mask(regs, 0x91, softOn ? BIT(4) : 0, BIT(4));
}

/*
 * Sets CX700 or later single chipset's LVDS1 software controlled
 * display period.
 */
static inline void
via_lvds1_set_soft_display_period(void __iomem *regs, bool softOn)
{
	/* Set LVDS1 software controlled display period state. */
	/* 3X5.91[7] - Software Direct On / Off Display Period
	 *             in the Panel Path
	 *             0: On
	 *             1: Off */
	svga_wcrt_mask(regs, 0x91, softOn ? 0 : BIT(7), BIT(7));
}

/*
 * Sets LVDS1 I/O pad state.
 */
static inline void
via_lvds1_set_io_pad_setting(void __iomem *regs, u8 io_pad_state)
{
	/* 3C5.2A[1:0] - LVDS1 I/O Pad Control
	 *               0x: Pad always off
	 *               10: Depend on the other control signal
	 *               11: Pad on/off according to the
	 *                   Power Management Status (PMS) */
	svga_wseq_mask(regs, 0x2A,
			io_pad_state, BIT(1) | BIT(0));
}

/*
 * Sets LVDS1 format.
 */
static inline void
via_lvds1_set_format(void __iomem *regs, u8 format)
{
	/* 3X5.D2[1] - LVDS Channel 1 Format Selection
	 *             0: SPWG Mode
	 *             1: OPENLDI Mode */
	svga_wcrt_mask(regs, 0xd2,
			format << 1, BIT(1));
}

/*
 * Sets LVDS1 output format (rotation or sequential mode).
 */
static inline void
via_lvds1_set_output_format(void __iomem *regs, u8 output_format)
{
	/* 3X5.88[6] - LVDS Channel 1 Output Format
	 *             0: Rotation
	 *             1: Sequential */
	svga_wcrt_mask(regs, 0x88,
			output_format << 6, BIT(6));
}

/*
 * Sets LVDS1 output color dithering (18-bit color display vs.
 * 24-bit color display).
 */
static inline void
via_lvds1_set_dithering(void __iomem *regs, bool dithering)
{
	/* 3X5.88[0] - LVDS Channel 1 Output Bits
	 *             0: 24 bits (dithering off)
	 *             1: 18 bits (dithering on) */
	svga_wcrt_mask(regs, 0x88,
			dithering ? BIT(0) : 0x00, BIT(0));
}

/*
 * Sets LVDS1 display source.
 */
static inline void
via_lvds1_set_display_source(void __iomem *regs, u8 display_source)
{
	/* 3X5.99[4] - LVDS Channel 1 Data Source Selection
	 *             0: Primary Display
	 *             1: Secondary Display */
	svga_wcrt_mask(regs, 0x99,
			display_source << 4, BIT(4));
}

/*
 * Sets CX700 / VX700 and VX800 chipset's LVDS2 power state.
 */
static inline void
via_lvds2_set_power(void __iomem *regs, bool power_state)
{
	/* 3X5.D2[6] - Power Down (Active High) for Channel 2 LVDS
	 *             0: Power on
	 *             1: Power off */
	svga_wcrt_mask(regs, 0xD2,
			power_state ? 0x00 : BIT(6), BIT(6));
}

/*
 * Sets LVDS2 I/O pad state.
 */
static inline void
via_lvds2_set_io_pad_setting(void __iomem *regs, u8 io_pad_state)
{
	/* 3C5.2A[3:2] - LVDS2 I/O Pad Control
	 *               0x: Pad always off
	 *               10: Depend on the other control signal
	 *               11: Pad on/off according to the
	 *                   Power Management Status (PMS) */
	svga_wseq_mask(regs, 0x2A,
			io_pad_state << 2, BIT(3) | BIT(2));
}

/*
 * Sets LVDS2 format.
 */
static inline void
via_lvds2_set_format(void __iomem *regs, u8 format)
{
	/* 3X5.D2[0] - LVDS Channel 2 Format Selection
	 *             0: SPWG Mode
	 *             1: OPENLDI Mode */
	svga_wcrt_mask(regs, 0xd2, format, BIT(0));
}

/*
 * Sets LVDS2 output format (rotation or sequential mode).
 */
static inline void
via_lvds2_set_output_format(void __iomem *regs, u8 output_format)
{
	/* 3X5.D4[7] - LVDS Channel 2 Output Format
	 *             0: Rotation
	 *             1: Sequential */
	svga_wcrt_mask(regs, 0xd4, output_format << 7, BIT(7));
}

/*
 * Sets LVDS2 output color dithering (18-bit color display vs.
 * 24-bit color display).
 */
static inline void
via_lvds2_set_dithering(void __iomem *regs, bool dithering)
{
	/* 3X5.D4[6] - LVDS Channel 2 Output Bits
	 *             0: 24 bits (dithering off)
	 *             1: 18 bits (dithering on) */
	svga_wcrt_mask(regs, 0xd4,
			dithering ? BIT(6) : 0x00, BIT(6));
}

/*
 * Sets LVDS2 display source.
 */
static inline void
via_lvds2_set_display_source(void __iomem *regs, u8 display_source)
{
	/* 3X5.97[4] - LVDS Channel 2 Data Source Selection
	 *             0: Primary Display
	 *             1: Secondary Display */
	svga_wcrt_mask(regs, 0x97,
			display_source << 4, BIT(4));
}

/*
 * Sets CX700 / VX700 and VX800 chipsets' TMDS (DVI) power state.
 */
static inline void
via_tmds_set_power(void __iomem *regs, bool powerState)
{
	/* 3X5.D2[3] - Power Down (Active High) for DVI
	 *             0: TMDS power on
	 *             1: TMDS power down */
	svga_wcrt_mask(regs, 0xD2,
			powerState ? 0x00 : BIT(3), BIT(3));
}

/*
 * Sets CX700 / VX700 and VX800 chipsets' TMDS (DVI) sync polarity.
 */
static inline void
via_tmds_set_sync_polarity(void __iomem *regs, u8 syncPolarity)
{
	/* Set TMDS (DVI) sync polarity. */
	/* 3X5.97[6] - DVI (TMDS) VSYNC Polarity
	 *              0: Positive
	 *              1: Negative
	 * 3X5.97[5] - DVI (TMDS) HSYNC Polarity
	 *              0: Positive
	 *              1: Negative */
	svga_wcrt_mask(regs, 0x97,
			syncPolarity << 5, BIT(6) | BIT(5));
}

/*
 * Sets TMDS (DVI) display source.
 */
static inline void
via_tmds_set_display_source(void __iomem *regs, u8 displaySource)
{
	/* The integrated TMDS transmitter appears to utilize LVDS1's
	 * data source selection bit (3X5.99[4]). */
	/* 3X5.99[4] - LVDS Channel1 Data Source Selection
	 *             0: Primary Display
	 *             1: Secondary Display */
	svga_wcrt_mask(regs, 0x99,
			displaySource << 4, BIT(4));
}


void load_register_tables(void __iomem *regbase, struct vga_registers *regs);
void load_value_to_registers(void __iomem *regbase, struct vga_registers *regs,
					unsigned int value);

#endif /* __CRTC_HW_H__ */
