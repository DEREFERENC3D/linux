/*
 * Copyright © 2019 Kevin Brace.
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

#ifndef _VIA_DRV_H
#define _VIA_DRV_H


#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>
#include <drm/drm_plane.h>

#include <drm/ttm/ttm_bo.h>
#include <drm/ttm/ttm_placement.h>

#include "via_crtc_hw.h"
#include "via_regs.h"


#define DRIVER_MAJOR		3
#define DRIVER_MINOR		6
#define DRIVER_PATCHLEVEL	7
#define DRIVER_NAME		"via"
#define DRIVER_DESC		"OpenChrome DRM for VIA Technologies Chrome"
#define DRIVER_DATE		"20230522"
#define DRIVER_AUTHOR		"OpenChrome Project"


#define VIA_TTM_PL_NUM		2

#define VIA_MAX_CRTC		2

#define VIA_CURSOR_SIZE		64

#define VIA_MM_ALIGN_SIZE	16


#define CLE266_REVISION_AX	0x0A
#define CLE266_REVISION_CX	0x0C

#define CX700_REVISION_700	0x0
#define CX700_REVISION_700M	0x1
#define CX700_REVISION_700M2	0x2

#define VIA_MEM_NONE		0x00
#define VIA_MEM_SDR66		0x01
#define VIA_MEM_SDR100		0x02
#define VIA_MEM_SDR133		0x03
#define VIA_MEM_DDR_200		0x04
#define VIA_MEM_DDR_266		0x05
#define VIA_MEM_DDR_333		0x06
#define VIA_MEM_DDR_400		0x07
#define VIA_MEM_DDR2_400	0x08
#define VIA_MEM_DDR2_533	0x09
#define VIA_MEM_DDR2_667	0x0A
#define VIA_MEM_DDR2_800	0x0B
#define VIA_MEM_DDR2_1066	0x0C
#define VIA_MEM_DDR3_533	0x0D
#define VIA_MEM_DDR3_667	0x0E
#define VIA_MEM_DDR3_800	0x0F
#define VIA_MEM_DDR3_1066	0x10
#define VIA_MEM_DDR3_1333	0x11
#define VIA_MEM_DDR3_1600	0x12

/* IGA Scaling disable */
#define VIA_NO_SCALING	0

/* IGA Scaling down */
#define VIA_HOR_SHRINK	BIT(0)
#define VIA_VER_SHRINK	BIT(1)
#define VIA_SHRINK	(BIT(0) | BIT(1))

/* IGA Scaling up */
#define VIA_HOR_EXPAND	BIT(2)
#define VIA_VER_EXPAND	BIT(3)
#define VIA_EXPAND	(BIT(2) | BIT(3))

/* Define IGA Scaling up/down status :  Horizontal or Vertical  */
/* Is IGA Hor scaling up/down status */
#define	HOR_SCALE	BIT(0)
/* Is IGA Ver scaling up/down status */
#define	VER_SCALE	BIT(1)
/* Is IGA Hor and Ver scaling up/down status */
#define	HOR_VER_SCALE	(BIT(0) | BIT(1))

#define	VIA_I2C_NONE	0x0
#define	VIA_I2C_BUS1	BIT(0)
#define	VIA_I2C_BUS2	BIT(1)
#define	VIA_I2C_BUS3	BIT(2)
#define	VIA_I2C_BUS4	BIT(3)
#define	VIA_I2C_BUS5	BIT(4)

#define VIA_DI_PORT_NONE	0x0
#define VIA_DI_PORT_DIP0	BIT(0)
#define VIA_DI_PORT_DIP1	BIT(1)
#define VIA_DI_PORT_DVP0	BIT(2)
#define VIA_DI_PORT_DVP1	BIT(3)
#define VIA_DI_PORT_DFPL	BIT(4)
#define VIA_DI_PORT_FPDPLOW	BIT(4)
#define VIA_DI_PORT_DFPH	BIT(5)
#define VIA_DI_PORT_FPDPHIGH	BIT(5)
#define VIA_DI_PORT_DFP		BIT(6)
#define VIA_DI_PORT_LVDS1	BIT(7)
#define VIA_DI_PORT_TMDS	BIT(7)
#define VIA_DI_PORT_LVDS2	BIT(8)

/* External TMDS (DVI) Transmitter Type */
#define	VIA_TMDS_NONE	0x0
#define	VIA_TMDS_VT1632	BIT(0)
#define	VIA_TMDS_SII164	BIT(1)


struct via_lvds_info {
	u32 x;
	u32 y;
};

struct via_crtc {
	struct drm_crtc base;
	struct crtc_timings pixel_timings;
	struct crtc_timings timings;
	struct vga_registers display_queue;
	struct vga_registers high_threshold;
	struct vga_registers threshold;
	struct vga_registers fifo_depth;
	struct vga_registers offset;
	struct vga_registers fetch;
	int scaling_mode;
	uint32_t index;
};

struct via_connector {
	struct drm_connector base;
	u32 i2c_bus;
	struct list_head props;
	uint32_t flags;
};

struct via_encoder {
	struct drm_encoder base;
	u32 i2c_bus;
	u32 di_port;
	struct via_connector cons[];
};

struct via_bo {
	struct ttm_buffer_object	ttm_bo;
	struct ttm_bo_kmap_obj		kmap;
	struct ttm_placement		placement;
	struct ttm_place		placements[VIA_TTM_PL_NUM];
};

struct via_drm_priv {
	struct drm_device dev;

	struct ttm_device		bdev;

	/* Set this flag for ttm_bo_device_init. */
	bool need_dma32;

	int revision;

	u8 vram_type;
	resource_size_t vram_start;
	resource_size_t vram_size;
	int vram_mtrr;

	resource_size_t mmio_base;
	resource_size_t mmio_size;
	void __iomem		*mmio;

	bool spread_spectrum;

	/*
	 * Frame Buffer Size Control register (SR14) needs to be saved and
	 * restored upon standby resume or can lead to a display corruption
	 * issue. These registers are only available on VX800, VX855, and
	 * VX900 chipsets. This bug was observed on VIA EPIA-M830 mainboard.
	 */
	uint8_t saved_sr14;

	/*
	 * GTI registers (SR66 through SR6F) need to be saved and restored
	 * upon standby resume or can lead to a display corruption issue.
	 * These registers are only available on VX800, VX855, and VX900
	 * chipsets. This bug was observed on VIA EPIA-M830 mainboard.
	 */
	uint8_t saved_sr66;
	uint8_t saved_sr67;
	uint8_t saved_sr68;
	uint8_t saved_sr69;
	uint8_t saved_sr6a;
	uint8_t saved_sr6b;
	uint8_t saved_sr6c;
	uint8_t saved_sr6d;
	uint8_t saved_sr6e;
	uint8_t saved_sr6f;

	/* 3X5.3B through 3X5.3F are scratch pad registers.
	 * They are important for FP detection. */
	uint8_t saved_cr3b;
	uint8_t saved_cr3c;
	uint8_t saved_cr3d;
	uint8_t saved_cr3e;
	uint8_t saved_cr3f;

	/*
	 * Due to the way VIA NanoBook reference design implemented
	 * the pin strapping settings, DRM needs to ignore them for
	 * FP and DVI to be properly detected.
	 */
	bool is_via_nanobook;

	/*
	 * Quanta IL1 netbook has its FP connected to DVP1
	 * rather than LVDS, hence, a special flag register
	 * is needed for properly controlling its FP.
	 */
	bool is_quanta_il1;

	/*
	 * Samsung NC20 netbook has its FP connected to LVDS2
	 * rather than the more logical LVDS1, hence, a special
	 * flag register is needed for properly controlling its
	 * FP.
	 */
	bool is_samsung_nc20;

	bool dac_presence;
	u32 dac_i2c_bus;

	bool int_tmds_presence;
	u32 int_tmds_di_port;
	u32 int_tmds_i2c_bus;

	bool ext_tmds_presence;
	u32 ext_tmds_di_port;
	u32 ext_tmds_i2c_bus;
	u32 ext_tmds_transmitter;

	bool int_fp1_presence;
	u32 int_fp1_di_port;
	u32 int_fp1_i2c_bus;

	bool int_fp2_presence;
	u32 int_fp2_di_port;
	u32 int_fp2_i2c_bus;

	/* Keeping track of the number of DVI connectors. */
	u32 number_dvi;

	/* Keeping track of the number of FP (Flat Panel) connectors. */
	u32 number_fp;

	u32 mapped_i2c_bus;
};


/*
 * Shortcut for using container_of macro.
 */
#define to_via_drm_priv(x)	container_of(x, struct via_drm_priv, dev)
#define to_ttm_bo(x)		container_of(x, struct via_bo, ttm_bo)


/* VIA MMIO register access */
#define VIA_BASE ((dev_priv->mmio))

#define VIA_READ(reg)		ioread32(VIA_BASE + reg)
#define VIA_WRITE(reg, val)	iowrite32(val, VIA_BASE + reg)
#define VIA_READ8(reg)		ioread8(VIA_BASE + reg)
#define VIA_WRITE8(reg, val)	iowrite8(val, VIA_BASE + reg)

#define VIA_WRITE_MASK(reg, data, mask) \
	VIA_WRITE(reg, (data & mask) | (VIA_READ(reg) & ~mask)) \

#define VGABASE (VIA_BASE+VIA_MMIO_VGABASE)


extern int via_driver_num_ioctls;

extern struct ttm_device_funcs via_bo_driver;

int via_vram_detect(struct via_drm_priv *dev_priv);
int via_vram_init(struct via_drm_priv *dev_priv);
void via_vram_fini(struct via_drm_priv *dev_priv);
int via_mmio_init(struct via_drm_priv *dev_priv);
void via_mmio_fini(struct via_drm_priv *dev_priv);
void via_graphics_unlock(struct via_drm_priv *dev_priv);
int via_device_init(struct via_drm_priv *dev_priv);
void via_device_fini(struct via_drm_priv *dev_priv);
void via_mode_config_init(struct via_drm_priv *dev_priv);
int via_modeset_init(struct drm_device *dev);
void via_modeset_fini(struct drm_device *dev);
int via_drm_init(struct drm_device *dev);
void via_drm_fini(struct drm_device *dev);

int via_dev_pm_ops_suspend(struct device *dev);
int via_dev_pm_ops_resume(struct device *dev);

void via_ttm_domain_to_placement(struct via_bo *bo, uint32_t ttm_domain);
void via_ttm_bo_destroy(struct ttm_buffer_object *tbo);
int via_bo_pin(struct via_bo *bo, uint32_t ttm_domain);
void via_bo_unpin(struct via_bo *bo);
int via_bo_create(struct drm_device *dev, struct ttm_device *bdev,
			uint64_t size, enum ttm_bo_type type,
			uint32_t ttm_domain, bool kmap,
			struct via_bo **bo_ptr);
void via_bo_destroy(struct via_bo *bo, bool kmap);
int via_mm_init(struct via_drm_priv *dev_priv);
void via_mm_fini(struct via_drm_priv *dev_priv);

void via_transmitter_io_pad_state(struct via_drm_priv *dev_priv,
					uint32_t di_port, bool io_pad_on);
void via_output_enable(struct via_drm_priv *dev_priv,
			uint32_t di_port, bool output_enable);
void via_clock_source(struct via_drm_priv *dev_priv, uint32_t di_port,
			bool clock_source);
void via_transmitter_clock_drive_strength(struct via_drm_priv *dev_priv,
					u32 di_port, u8 drive_strength);
void via_transmitter_data_drive_strength(struct via_drm_priv *dev_priv,
					u32 di_port, u8 drive_strength);
void via_transmitter_display_source(struct via_drm_priv *dev_priv,
					u32 di_port, int index);

void via_encoder_destroy(struct drm_encoder *encoder);

void via_connector_destroy(struct drm_connector *connector);

extern const struct drm_plane_helper_funcs via_cursor_drm_plane_helper_funcs;
extern const struct drm_plane_funcs via_cursor_drm_plane_funcs;
extern const uint32_t via_cursor_formats[];
extern const unsigned int via_cursor_formats_size;

/* i2c */
struct i2c_adapter *via_find_ddc_bus(int port);
void via_i2c_readbytes(struct i2c_adapter *adapter,
			u8 slave_addr, char offset,
			u8 *buffer, unsigned int size);
void via_i2c_writebytes(struct i2c_adapter *adapter,
			u8 slave_addr, char offset,
			u8 *data, unsigned int size);
void via_i2c_reg_init(struct via_drm_priv *dev_priv);
int via_i2c_init(struct drm_device *dev);
void via_i2c_exit(void);

/* clock */
u32 via_get_clk_value(struct drm_device *dev, u32 clk);
void via_set_vclock(struct drm_crtc *crtc, u32 clk);

/* crtc */
void via_load_crtc_pixel_timing(struct drm_crtc *crtc,
				struct drm_display_mode *mode);
int via_crtc_init(struct via_drm_priv *dev_priv, uint32_t index);

void via_dac_probe(struct drm_device *dev);
bool via_vt1632_probe(struct i2c_adapter *i2c_bus);
bool via_sii164_probe(struct i2c_adapter *i2c_bus);
void via_ext_dvi_probe(struct drm_device *dev);
void via_tmds_probe(struct drm_device *dev);
void via_lvds_probe(struct drm_device *dev);

void via_hdmi_init(struct drm_device *dev, u32 di_port);
void via_dac_init(struct drm_device *dev);
void via_vt1632_init(struct drm_device *dev);
void via_sii164_init(struct drm_device *dev);
void via_ext_dvi_init(struct drm_device *dev);
void via_tmds_init(struct drm_device *dev);
void via_lvds_init(struct drm_device *dev);

int via_gem_alloc_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv);
int via_gem_mmap_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);

#endif /* _VIA_DRV_H */
