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
#include <drm/drm_legacy.h>
#include <drm/drm_mm.h>
#include <drm/drm_plane.h>

#include <drm/ttm/ttm_bo_api.h>
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_placement.h>

#include <drm/via_drm.h>

#include <linux/atomic.h>
#include <linux/hrtimer.h>
#include <linux/idr.h>
#include <linux/mutex.h>
#include <linux/wait.h>

#include "via_crtc_hw.h"
#include "via_regs.h"


#define DRIVER_MAJOR		3
#define DRIVER_MINOR		6
#define DRIVER_PATCHLEVEL	14
#define DRIVER_NAME		"via"
#define DRIVER_DESC		"OpenChrome DRM for VIA Technologies Chrome"
#define DRIVER_DATE		"20240215"
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

#define VIA_PCI_BUF_SIZE		60000
#define VIA_FIRE_BUF_SIZE		1024
#define VIA_NUM_IRQS		4

#define VIA_MM_ALIGN_SHIFT		4
#define VIA_MM_ALIGN_MASK		((1 << VIA_MM_ALIGN_SHIFT) - 1)

enum via_family {
	VIA_OTHER = 0,		/* Baseline */
	VIA_PRO_GROUP_A,	/* Another video engine and DMA commands */
	VIA_DX9_0		/* Same video as pro_group_a, but 3D unsupported */
};

typedef enum {
	no_sequence = 0,
	z_address,
	dest_address,
	tex_address
} drm_via_sequence_t;

typedef struct {
	unsigned texture;
	uint32_t z_addr;
	uint32_t d_addr;
	uint32_t t_addr[2][10];
	uint32_t pitch[2][10];
	uint32_t height[2][10];
	uint32_t tex_level_lo[2];
	uint32_t tex_level_hi[2];
	uint32_t tex_palette_size[2];
	uint32_t tex_npot[2];
	drm_via_sequence_t unfinished;
	int agp_texture;
	int multitex;
	struct drm_device *dev;
	drm_local_map_t *map_cache;
	uint32_t vertex_count;
	int agp;
	const uint32_t *buf_start;
} drm_via_state_t;

typedef uint32_t maskarray_t[5];

typedef struct drm_via_irq {
	atomic_t irq_received;
	uint32_t pending_mask;
	uint32_t enable_mask;
	wait_queue_head_t irq_queue;
} drm_via_irq_t;

typedef struct drm_via_ring_buffer {
	drm_local_map_t map;
	char *virtual_start;
} drm_via_ring_buffer_t;

struct via_memblock {
	struct drm_mm_node mm_node;
	struct list_head owner_list;
	int idr_key;
};

struct via_file_private {
	struct list_head obj_list;
};


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
	struct hrtimer vblank_hrtimer;
	ktime_t vblank_period_ns;
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
	resource_size_t gtt_size;
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

	/*
	 * Serializes access to the command (DMA ring) buffer. In the legacy
	 * DRI stack this was done with the DRM hardware lock, but DRI2
	 * clients do not use that lock, so the kernel must serialize ring
	 * submissions itself.
	 */
	struct mutex ring_lock;

	/* VIA 3D engine command ring buffer. */
	unsigned long agpAddr;
	char *dma_ptr;
	unsigned int dma_low;
	unsigned int dma_high;
	unsigned int dma_offset;
	uint32_t dma_wrap;
	volatile uint32_t *last_pause_ptr;
	volatile uint32_t *hw_addr_ptr;
	drm_via_ring_buffer_t ring;
	drm_via_state_t hc_state;
	char pci_buf[VIA_PCI_BUF_SIZE];
	const uint32_t *fire_offsets[VIA_FIRE_BUF_SIZE];
	uint32_t num_fire_offsets;
	uint32_t dma_diff;

	/* IRQ / vblank state. */
	drm_via_irq_t via_irqs[VIA_NUM_IRQS];
	unsigned num_irqs;
	maskarray_t *irq_masks;
	uint32_t irq_enable_mask;
	uint32_t irq_pending_mask;
	int *irq_map;
	ktime_t last_vblank;
	int last_vblank_valid;
	ktime_t nsec_per_vblank;
	atomic_t vbl_received;

	/* Legacy memory managers used by VIA_ALLOCMEM / VIA_FREEMEM. */
	int vram_initialized;
	struct drm_mm vram_mm;
	int agp_initialized;
	struct drm_mm agp_mm;
	struct idr object_idr;
	unsigned long vram_offset;
	unsigned long agp_offset;

	int chipset;
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


/* via_connector.c */
void via_connector_destroy(struct drm_connector *connector);

/* via_crtc.c */
void via_load_crtc_pixel_timing(struct drm_crtc *crtc,
				struct drm_display_mode *mode);
int via_crtc_init(struct via_drm_priv *dev_priv, uint32_t index);
void via_dac_probe(struct drm_device *dev);
bool via_vt1632_probe(struct drm_device *dev,
			struct i2c_adapter *i2c_bus);
bool via_sii164_probe(struct drm_device *dev,
			struct i2c_adapter *i2c_bus);
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

/* via_cursor.c */
extern const struct drm_plane_helper_funcs via_cursor_drm_plane_helper_funcs;
extern const struct drm_plane_funcs via_cursor_drm_plane_funcs;
extern const uint32_t via_cursor_formats[];
extern const unsigned int via_cursor_formats_size;

/* via_encoder.c */
void via_encoder_destroy(struct drm_encoder *encoder);

/* via_i2c.c */
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

/* via_init.c */
int via_drm_init(struct drm_device *dev);
void via_drm_fini(struct drm_device *dev);

/* via_ioctl.c */
int via_gem_alloc_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv);
int via_gem_mmap_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);

/* via_object.c */
void via_ttm_domain_to_placement(struct via_bo *bo, uint32_t ttm_domain);
void via_ttm_bo_destroy(struct ttm_buffer_object *tbo);
int via_bo_pin(struct via_bo *bo, uint32_t ttm_domain);
void via_bo_unpin(struct via_bo *bo);
int via_bo_create(struct drm_device *dev, struct ttm_device *bdev,
			uint64_t size, enum ttm_bo_type type,
			uint32_t ttm_domain, bool kmap,
			struct via_bo **bo_ptr);
void via_bo_destroy(struct via_bo *bo, bool kmap);
int via_mm_init(struct drm_device *dev);
void via_mm_fini(struct drm_device *dev);

/* via_pll.c */
u32 via_get_clk_value(struct drm_device *dev, u32 clk);
void via_set_vclock(struct drm_crtc *crtc, u32 clk);

/* via_pm.c */
int via_dev_pm_ops_suspend(struct device *dev);
int via_dev_pm_ops_resume(struct device *dev);

/* via_ttm.c */
extern struct ttm_device_funcs via_bo_driver;
void via_ttm_debugfs_init(struct drm_minor *minor);
void via_ttm_debugfs_remove(struct drm_minor *minor);

/* via_ring.c */
int via_ring_legacy_init(struct drm_device *dev);
void via_ring_legacy_fini(struct drm_device *dev);
int via_dma_init_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int via_cmdbuffer_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int via_pci_cmdbuffer_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file_priv);
int via_cmdbuf_size_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file_priv);
int via_flush_ioctl(struct drm_device *dev, void *data,
		    struct drm_file *file_priv);
int via_wait_irq_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv);
int via_agp_init_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv);
int via_fb_init_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);
int via_mem_alloc_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int via_mem_free_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv);
int via_driver_dma_quiescent(struct drm_device *dev);
void via_driver_postclose(struct drm_device *dev, struct drm_file *file_priv);
void via_ring_lastclose(struct drm_device *dev);
u32 via_get_vblank_counter(struct drm_device *dev, unsigned int pipe);
int via_enable_vblank(struct drm_device *dev, unsigned int pipe);
void via_disable_vblank(struct drm_device *dev, unsigned int pipe);
irqreturn_t via_ring_irq_handler(int irq, void *arg);

/* via_tx.c */
void via_transmitter_io_pad_state(struct drm_device *dev,
					uint32_t di_port,
					bool io_pad_on);
void via_output_enable(struct drm_device *dev,
			uint32_t di_port, bool output_enable);
void via_clock_source(struct drm_device *dev,
			uint32_t di_port, bool clock_source);
void via_transmitter_clock_drive_strength(struct drm_device *dev,
						u32 di_port,
						u8 drive_strength);
void via_transmitter_data_drive_strength(struct drm_device *dev,
						u32 di_port,
						u8 drive_strength);
void via_transmitter_display_source(struct drm_device *dev,
					u32 di_port, int index);

#endif /* _VIA_DRV_H */
