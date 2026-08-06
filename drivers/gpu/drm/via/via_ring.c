/*
 * VIA Unichrome 3D engine command ring buffer support.
 *
 * This file re-instates the legacy VIA 3D engine DMA ring machinery
 * (previously found in via_dri1.c) inside the KMS based DRM driver.
 * It provides the command ring ioctls that the Mesa unichrome driver
 * uses to submit 3D command streams, as well as the AGP/frame-buffer
 * memory managers and the interrupt / vblank support.
 *
 * Copyright 1998-2003 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2003 S3 Graphics, Inc. All Rights Reserved.
 * Copyright 2002 Tungsten Graphics, Inc.
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas. All Rights Reserved.
 * Copyright 2006 Tungsten Graphics Inc., Bismarck, ND., USA.
 * Copyright 2004 Digeo, Inc., Palo Alto, CA, U.S.A. All Rights Reserved.
 * Copyright 2004 The Unichrome project. All Rights Reserved.
 * Copyright 2004 BEAM Ltd.
 * Copyright 2005 Thomas Hellstrom. All Rights Reserved.
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
 * VIA, S3 GRAPHICS, AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/pci.h>
#include <linux/uaccess.h>

#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_legacy.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>

#include "via_3d_reg.h"
#include "via_drv.h"

/* VIA MMIO register access */
static inline u32 via_read(struct via_drm_priv *dev_priv, u32 reg)
{
	return ioread32(dev_priv->mmio + reg);
}

static inline void via_write(struct via_drm_priv *dev_priv, u32 reg,
			     u32 val)
{
	iowrite32(val, dev_priv->mmio + reg);
}

static inline void via_write8(struct via_drm_priv *dev_priv, u32 reg,
			      u32 val)
{
	iowrite8(val, dev_priv->mmio + reg);
}

static inline void via_write8_mask(struct via_drm_priv *dev_priv,
				   u32 reg, u32 mask, u32 val)
{
	u32 tmp;

	tmp = ioread8(dev_priv->mmio + reg);
	tmp = (tmp & ~mask) | (val & mask);
	iowrite8(tmp, dev_priv->mmio + reg);
}

/*
 * Poll in a loop waiting for 'condition' to be true.
 * Note: A direct replacement with wait_event_interruptible_timeout()
 *       will not work unless driver is updated to emit wake_up()
 *       in relevant places that can impact the 'condition'
 *
 * Returns:
 *   ret keeps current value if 'condition' becomes true
 *   ret = -EBUSY if timeout happens
 *   ret = -EINTR if a signal interrupted the waiting period
 */
#define VIA_WAIT_ON( ret, queue, timeout, condition )		\
do {								\
	DECLARE_WAITQUEUE(entry, current);			\
	unsigned long end = jiffies + (timeout);		\
	add_wait_queue(&(queue), &entry);			\
								\
	for (;;) {						\
		__set_current_state(TASK_INTERRUPTIBLE);	\
		if (condition)					\
			break;					\
		if (time_after_eq(jiffies, end)) {		\
			ret = -EBUSY;				\
			break;					\
		}						\
		schedule_timeout((HZ/100 > 1) ? HZ/100 : 1);	\
		if (signal_pending(current)) {			\
			ret = -EINTR;				\
			break;					\
		}						\
	}							\
	__set_current_state(TASK_RUNNING);			\
	remove_wait_queue(&(queue), &entry);			\
} while (0)

#define CMDBUF_ALIGNMENT_SIZE   (0x100)
#define CMDBUF_ALIGNMENT_MASK   (0x0ff)

/* defines for VIA 3D registers */
#define VIA_REG_STATUS          0x400
#define VIA_REG_TRANSET         0x43C
#define VIA_REG_TRANSPACE       0x440

/* VIA_REG_STATUS(0x400): Engine Status */
#define VIA_VR_QUEUE_BUSY       0x00020000	/* Virtual Queue is busy */

#define SetReg2DAGP(nReg, nData) {				\
	*((uint32_t *)(vb)) = ((nReg) >> 2) | HALCYON_HEADER1;	\
	*((uint32_t *)(vb) + 1) = (nData);			\
	vb = ((uint32_t *)vb) + 2;				\
	dev_priv->dma_low += 8;					\
}

#define via_flush_write_combine() mb()

#define VIA_OUT_RING_QW(w1, w2)	do {		\
	*vb++ = (w1);				\
	*vb++ = (w2);				\
	dev_priv->dma_low += 8;			\
} while (0)

#define VIA_REG_INTERRUPT       0x200

/* VIA_REG_INTERRUPT */
#define VIA_IRQ_GLOBAL	  (1 << 31)
#define VIA_IRQ_VBLANK_ENABLE   (1 << 19)
#define VIA_IRQ_VBLANK_PENDING  (1 << 3)
#define VIA_IRQ_HQV0_ENABLE     (1 << 11)
#define VIA_IRQ_HQV1_ENABLE     (1 << 25)
#define VIA_IRQ_HQV0_PENDING    (1 << 9)
#define VIA_IRQ_HQV1_PENDING    (1 << 10)
#define VIA_IRQ_DMA0_DD_ENABLE  (1 << 20)
#define VIA_IRQ_DMA0_TD_ENABLE  (1 << 21)
#define VIA_IRQ_DMA1_DD_ENABLE  (1 << 22)
#define VIA_IRQ_DMA1_TD_ENABLE  (1 << 23)
#define VIA_IRQ_DMA0_DD_PENDING (1 << 4)
#define VIA_IRQ_DMA0_TD_PENDING (1 << 5)
#define VIA_IRQ_DMA1_DD_PENDING (1 << 6)
#define VIA_IRQ_DMA1_TD_PENDING (1 << 7)

/*
 *  PCI DMA Registers
 *  Channels 2 & 3 don't seem to be implemented in hardware.
 */

#define VIA_PCI_DMA_MAR0            0xE40   /* Memory Address Register of Channel 0 */
#define VIA_PCI_DMA_DAR0            0xE44   /* Device Address Register of Channel 0 */
#define VIA_PCI_DMA_BCR0            0xE48   /* Byte Count Register of Channel 0 */
#define VIA_PCI_DMA_DPR0            0xE4C   /* Descriptor Pointer Register of Channel 0 */

#define VIA_PCI_DMA_MAR1            0xE50   /* Memory Address Register of Channel 1 */
#define VIA_PCI_DMA_DAR1            0xE54   /* Device Address Register of Channel 1 */
#define VIA_PCI_DMA_BCR1            0xE58   /* Byte Count Register of Channel 1 */
#define VIA_PCI_DMA_DPR1            0xE5C   /* Descriptor Pointer Register of Channel 1 */

#define VIA_PCI_DMA_MAR2            0xE60   /* Memory Address Register of Channel 2 */
#define VIA_PCI_DMA_DAR2            0xE64   /* Device Address Register of Channel 2 */
#define VIA_PCI_DMA_BCR2            0xE68   /* Byte Count Register of Channel 2 */
#define VIA_PCI_DMA_DPR2            0xE6C   /* Descriptor Pointer Register of Channel 2 */

#define VIA_PCI_DMA_MAR3            0xE70   /* Memory Address Register of Channel 3 */
#define VIA_PCI_DMA_DAR3            0xE74   /* Device Address Register of Channel 3 */
#define VIA_PCI_DMA_BCR3            0xE78   /* Byte Count Register of Channel 3 */
#define VIA_PCI_DMA_DPR3            0xE7C   /* Descriptor Pointer Register of Channel 3 */

#define VIA_PCI_DMA_MR0             0xE80   /* Mode Register of Channel 0 */
#define VIA_PCI_DMA_MR1             0xE84   /* Mode Register of Channel 1 */
#define VIA_PCI_DMA_MR2             0xE88   /* Mode Register of Channel 2 */
#define VIA_PCI_DMA_MR3             0xE8C   /* Mode Register of Channel 3 */

#define VIA_PCI_DMA_CSR0            0xE90   /* Command/Status Register of Channel 0 */
#define VIA_PCI_DMA_CSR1            0xE94   /* Command/Status Register of Channel 1 */
#define VIA_PCI_DMA_CSR2            0xE98   /* Command/Status Register of Channel 2 */
#define VIA_PCI_DMA_CSR3            0xE9C   /* Command/Status Register of Channel 3 */

#define VIA_PCI_DMA_PTR             0xEA0   /* Priority Type Register */

/* Define for DMA engine */
/* DPR */
#define VIA_DMA_DPR_EC		(1<<1)	/* end of chain */
#define VIA_DMA_DPR_DDIE	(1<<2)	/* descriptor done interrupt enable */
#define VIA_DMA_DPR_DT		(1<<3)	/* direction of transfer (RO) */

/* MR */
#define VIA_DMA_MR_CM		(1<<0)	/* chaining mode */
#define VIA_DMA_MR_TDIE		(1<<1)	/* transfer done interrupt enable */
#define VIA_DMA_MR_HENDMACMD		(1<<7) /* ? */

/* CSR */
#define VIA_DMA_CSR_DE		(1<<0)	/* DMA enable */
#define VIA_DMA_CSR_TS		(1<<1)	/* transfer start */
#define VIA_DMA_CSR_TA		(1<<2)	/* transfer abort */
#define VIA_DMA_CSR_TD		(1<<3)	/* transfer done */
#define VIA_DMA_CSR_DD		(1<<4)	/* descriptor done */

/*
 * Device-specific IRQs go here. Currently we activate the HQV interrupts of
 * Unichrome Pro group A.
 */

static maskarray_t via_pro_group_a_irqs[] = {
	{VIA_IRQ_HQV0_ENABLE, VIA_IRQ_HQV0_PENDING, 0x000003D0, 0x00008010,
	 0x00000000 },
	{VIA_IRQ_HQV1_ENABLE, VIA_IRQ_HQV1_PENDING, 0x000013D0, 0x00008010,
	 0x00000000 },
	{VIA_IRQ_DMA0_TD_ENABLE, VIA_IRQ_DMA0_TD_PENDING, VIA_PCI_DMA_CSR0,
	 VIA_DMA_CSR_TA | VIA_DMA_CSR_TD, 0x00000008},
	{VIA_IRQ_DMA1_TD_ENABLE, VIA_IRQ_DMA1_TD_PENDING, VIA_PCI_DMA_CSR1,
	 VIA_DMA_CSR_TA | VIA_DMA_CSR_TD, 0x00000008},
};
static int via_num_pro_group_a = ARRAY_SIZE(via_pro_group_a_irqs);
static int via_irqmap_pro_group_a[] = {0, 1, -1, 2, -1, 3};

static maskarray_t via_unichrome_irqs[] = {
	{VIA_IRQ_DMA0_TD_ENABLE, VIA_IRQ_DMA0_TD_PENDING, VIA_PCI_DMA_CSR0,
	 VIA_DMA_CSR_TA | VIA_DMA_CSR_TD, 0x00000008},
	{VIA_IRQ_DMA1_TD_ENABLE, VIA_IRQ_DMA1_TD_PENDING, VIA_PCI_DMA_CSR1,
	 VIA_DMA_CSR_TA | VIA_DMA_CSR_TD, 0x00000008}
};
static int via_num_unichrome = ARRAY_SIZE(via_unichrome_irqs);
static int via_irqmap_unichrome[] = {-1, -1, -1, 0, -1, 1};

typedef enum {
	state_command,
	state_header2,
	state_header1,
	state_vheader5,
	state_vheader6,
	state_error
} verifier_state_t;

typedef enum {
	no_check = 0,
	check_for_header2,
	check_for_header1,
	check_for_header2_err,
	check_for_header1_err,
	check_for_fire,
	check_z_buffer_addr0,
	check_z_buffer_addr1,
	check_z_buffer_addr_mode,
	check_destination_addr0,
	check_destination_addr1,
	check_destination_addr_mode,
	check_for_dummy,
	check_for_dd,
	check_texture_addr0,
	check_texture_addr1,
	check_texture_addr2,
	check_texture_addr3,
	check_texture_addr4,
	check_texture_addr5,
	check_texture_addr6,
	check_texture_addr7,
	check_texture_addr8,
	check_texture_addr_mode,
	check_for_vertex_count,
	check_number_texunits,
	forbidden_command
} hazard_t;

/*
 * Associates each hazard above with a possible multi-command
 * sequence. For example an address that is split over multiple
 * commands and that needs to be checked at the first command
 * that does not include any part of the address.
 */

static drm_via_sequence_t seqs[] = {
	no_sequence,
	no_sequence,
	no_sequence,
	no_sequence,
	no_sequence,
	no_sequence,
	z_address,
	z_address,
	z_address,
	dest_address,
	dest_address,
	dest_address,
	no_sequence,
	no_sequence,
	tex_address,
	tex_address,
	tex_address,
	tex_address,
	tex_address,
	tex_address,
	tex_address,
	tex_address,
	tex_address,
	tex_address,
	no_sequence
};

typedef struct {
	unsigned int code;
	hazard_t hz;
} hz_init_t;

static hz_init_t init_table1[] = {
	{0xf2, check_for_header2_err},
	{0xf0, check_for_header1_err},
	{0xee, check_for_fire},
	{0xcc, check_for_dummy},
	{0xdd, check_for_dd},
	{0x00, no_check},
	{0x10, check_z_buffer_addr0},
	{0x11, check_z_buffer_addr1},
	{0x12, check_z_buffer_addr_mode},
	{0x13, no_check},
	{0x14, no_check},
	{0x15, no_check},
	{0x23, no_check},
	{0x24, no_check},
	{0x33, no_check},
	{0x34, no_check},
	{0x35, no_check},
	{0x36, no_check},
	{0x37, no_check},
	{0x38, no_check},
	{0x39, no_check},
	{0x3A, no_check},
	{0x3B, no_check},
	{0x3C, no_check},
	{0x3D, no_check},
	{0x3E, no_check},
	{0x40, check_destination_addr0},
	{0x41, check_destination_addr1},
	{0x42, check_destination_addr_mode},
	{0x43, no_check},
	{0x44, no_check},
	{0x50, no_check},
	{0x51, no_check},
	{0x52, no_check},
	{0x53, no_check},
	{0x54, no_check},
	{0x55, no_check},
	{0x56, no_check},
	{0x57, no_check},
	{0x58, no_check},
	{0x70, no_check},
	{0x71, no_check},
	{0x78, no_check},
	{0x79, no_check},
	{0x7A, no_check},
	{0x7B, no_check},
	{0x7C, no_check},
	{0x7D, check_for_vertex_count}
};

static hz_init_t init_table2[] = {
	{0xf2, check_for_header2_err},
	{0xf0, check_for_header1_err},
	{0xee, check_for_fire},
	{0xcc, check_for_dummy},
	{0x00, check_texture_addr0},
	{0x01, check_texture_addr0},
	{0x02, check_texture_addr0},
	{0x03, check_texture_addr0},
	{0x04, check_texture_addr0},
	{0x05, check_texture_addr0},
	{0x06, check_texture_addr0},
	{0x07, check_texture_addr0},
	{0x08, check_texture_addr0},
	{0x09, check_texture_addr0},
	{0x20, check_texture_addr1},
	{0x21, check_texture_addr1},
	{0x22, check_texture_addr1},
	{0x23, check_texture_addr4},
	{0x2B, check_texture_addr3},
	{0x2C, check_texture_addr3},
	{0x2D, check_texture_addr3},
	{0x2E, check_texture_addr3},
	{0x2F, check_texture_addr3},
	{0x30, check_texture_addr3},
	{0x31, check_texture_addr3},
	{0x32, check_texture_addr3},
	{0x33, check_texture_addr3},
	{0x34, check_texture_addr3},
	{0x4B, check_texture_addr5},
	{0x4C, check_texture_addr6},
	{0x51, check_texture_addr7},
	{0x52, check_texture_addr8},
	{0x77, check_texture_addr2},
	{0x78, no_check},
	{0x79, no_check},
	{0x7A, no_check},
	{0x7B, check_texture_addr_mode},
	{0x7C, no_check},
	{0x7D, no_check},
	{0x7E, no_check},
	{0x7F, no_check},
	{0x80, no_check},
	{0x81, no_check},
	{0x82, no_check},
	{0x83, no_check},
	{0x85, no_check},
	{0x86, no_check},
	{0x87, no_check},
	{0x88, no_check},
	{0x89, no_check},
	{0x8A, no_check},
	{0x90, no_check},
	{0x91, no_check},
	{0x92, no_check},
	{0x93, no_check}
};

static hz_init_t init_table3[] = {
	{0xf2, check_for_header2_err},
	{0xf0, check_for_header1_err},
	{0xcc, check_for_dummy},
	{0x00, check_number_texunits}
};

static hazard_t table1[256];
static hazard_t table2[256];
static hazard_t table3[256];

static __inline__ int
eat_words(const uint32_t **buf, const uint32_t *buf_end, unsigned num_words)
{
	if ((buf_end - *buf) >= num_words) {
		*buf += num_words;
		return 0;
	}
	DRM_ERROR("Illegal termination of DMA command buffer\n");
	return 1;
}

static __inline__ drm_local_map_t *via_drm_lookup_agp_map(drm_via_state_t *seq,
						    unsigned long offset,
						    unsigned long size,
						    struct drm_device *dev)
{
	struct drm_map_list *r_list;
	drm_local_map_t *map = seq->map_cache;

	if (map && map->offset <= offset
	    && (offset + size) <= (map->offset + map->size)) {
		return map;
	}

	list_for_each_entry(r_list, &dev->maplist, head) {
		map = r_list->map;
		if (!map)
			continue;
		if (map->offset <= offset
		    && (offset + size) <= (map->offset + map->size)
		    && !(map->flags & _DRM_RESTRICTED)
		    && (map->type == _DRM_AGP)) {
			seq->map_cache = map;
			return map;
		}
	}
	return NULL;
}

/*
 * Require that all AGP texture levels reside in the same AGP map which should
 * be mappable by the client. This is not a big restriction.
 */
static __inline__ int finish_current_sequence(drm_via_state_t *cur_seq)
{
	switch (cur_seq->unfinished) {
	case z_address:
		DRM_DEBUG("Z Buffer start address is 0x%x\n", cur_seq->z_addr);
		break;
	case dest_address:
		DRM_DEBUG("Destination start address is 0x%x\n",
			  cur_seq->d_addr);
		break;
	case tex_address:
		if (cur_seq->agp_texture) {
			unsigned start =
			    cur_seq->tex_level_lo[cur_seq->texture];
			unsigned end = cur_seq->tex_level_hi[cur_seq->texture];
			unsigned long lo = ~0, hi = 0, tmp;
			uint32_t *addr, *pitch, *height, tex;
			unsigned i;
			int npot;

			if (end > 9)
				end = 9;
			if (start > 9)
				start = 9;

			addr =
			    &(cur_seq->t_addr[tex = cur_seq->texture][start]);
			pitch = &(cur_seq->pitch[tex][start]);
			height = &(cur_seq->height[tex][start]);
			npot = cur_seq->tex_npot[tex];
			for (i = start; i <= end; ++i) {
				tmp = *addr++;
				if (tmp < lo)
					lo = tmp;
				if (i == 0 && npot)
					tmp += (*height++ * *pitch++);
				else
					tmp += (*height++ << *pitch++);
				if (tmp > hi)
					hi = tmp;
			}

			if (!via_drm_lookup_agp_map
			    (cur_seq, lo, hi - lo, cur_seq->dev)) {
				DRM_ERROR
				    ("AGP texture is not in allowed map\n");
				return 2;
			}
		}
		break;
	default:
		break;
	}
	cur_seq->unfinished = no_sequence;
	return 0;
}

static __inline__ int
investigate_hazard(uint32_t cmd, hazard_t hz, drm_via_state_t *cur_seq)
{
	register uint32_t tmp, *tmp_addr;

	if (cur_seq->unfinished && (cur_seq->unfinished != seqs[hz])) {
		int ret;
		if ((ret = finish_current_sequence(cur_seq)))
			return ret;
	}

	switch (hz) {
	case check_for_header2:
		if (cmd == HALCYON_HEADER2)
			return 1;
		return 0;
	case check_for_header1:
		if ((cmd & HALCYON_HEADER1MASK) == HALCYON_HEADER1)
			return 1;
		return 0;
	case check_for_header2_err:
		if (cmd == HALCYON_HEADER2)
			return 1;
		DRM_ERROR("Illegal DMA HALCYON_HEADER2 command\n");
		break;
	case check_for_header1_err:
		if ((cmd & HALCYON_HEADER1MASK) == HALCYON_HEADER1)
			return 1;
		DRM_ERROR("Illegal DMA HALCYON_HEADER1 command\n");
		break;
	case check_for_fire:
		if ((cmd & HALCYON_FIREMASK) == HALCYON_FIRECMD)
			return 1;
		DRM_ERROR("Illegal DMA HALCYON_FIRECMD command\n");
		break;
	case check_for_dummy:
		if (HC_DUMMY == cmd)
			return 0;
		DRM_ERROR("Illegal DMA HC_DUMMY command\n");
		break;
	case check_for_dd:
		if (0xdddddddd == cmd)
			return 0;
		DRM_ERROR("Illegal DMA 0xdddddddd command\n");
		break;
	case check_z_buffer_addr0:
		cur_seq->unfinished = z_address;
		cur_seq->z_addr = (cur_seq->z_addr & 0xFF000000) |
		    (cmd & 0x00FFFFFF);
		return 0;
	case check_z_buffer_addr1:
		cur_seq->unfinished = z_address;
		cur_seq->z_addr = (cur_seq->z_addr & 0x00FFFFFF) |
		    ((cmd & 0xFF) << 24);
		return 0;
	case check_z_buffer_addr_mode:
		cur_seq->unfinished = z_address;
		if ((cmd & 0x0000C000) == 0)
			return 0;
		DRM_ERROR("Attempt to place Z buffer in system memory\n");
		return 2;
	case check_destination_addr0:
		cur_seq->unfinished = dest_address;
		cur_seq->d_addr = (cur_seq->d_addr & 0xFF000000) |
		    (cmd & 0x00FFFFFF);
		return 0;
	case check_destination_addr1:
		cur_seq->unfinished = dest_address;
		cur_seq->d_addr = (cur_seq->d_addr & 0x00FFFFFF) |
		    ((cmd & 0xFF) << 24);
		return 0;
	case check_destination_addr_mode:
		cur_seq->unfinished = dest_address;
		if ((cmd & 0x0000C000) == 0)
			return 0;
		DRM_ERROR
		    ("Attempt to place 3D drawing buffer in system memory\n");
		return 2;
	case check_texture_addr0:
		cur_seq->unfinished = tex_address;
		tmp = (cmd >> 24);
		tmp_addr = &cur_seq->t_addr[cur_seq->texture][tmp];
		*tmp_addr = (*tmp_addr & 0xFF000000) | (cmd & 0x00FFFFFF);
		return 0;
	case check_texture_addr1:
		cur_seq->unfinished = tex_address;
		tmp = ((cmd >> 24) - 0x20);
		tmp += tmp << 1;
		tmp_addr = &cur_seq->t_addr[cur_seq->texture][tmp];
		*tmp_addr = (*tmp_addr & 0x00FFFFFF) | ((cmd & 0xFF) << 24);
		tmp_addr++;
		*tmp_addr = (*tmp_addr & 0x00FFFFFF) | ((cmd & 0xFF00) << 16);
		tmp_addr++;
		*tmp_addr = (*tmp_addr & 0x00FFFFFF) | ((cmd & 0xFF0000) << 8);
		return 0;
	case check_texture_addr2:
		cur_seq->unfinished = tex_address;
		cur_seq->tex_level_lo[tmp = cur_seq->texture] = cmd & 0x3F;
		cur_seq->tex_level_hi[tmp] = (cmd & 0xFC0) >> 6;
		return 0;
	case check_texture_addr3:
		cur_seq->unfinished = tex_address;
		tmp = ((cmd >> 24) - HC_SubA_HTXnL0Pit);
		if (tmp == 0 &&
		    (cmd & HC_HTXnEnPit_MASK)) {
			cur_seq->pitch[cur_seq->texture][tmp] =
				(cmd & HC_HTXnLnPit_MASK);
			cur_seq->tex_npot[cur_seq->texture] = 1;
		} else {
			cur_seq->pitch[cur_seq->texture][tmp] =
				(cmd & HC_HTXnLnPitE_MASK) >> HC_HTXnLnPitE_SHIFT;
			cur_seq->tex_npot[cur_seq->texture] = 0;
			if (cmd & 0x000FFFFF) {
				DRM_ERROR
					("Unimplemented texture level 0 pitch mode.\n");
				return 2;
			}
		}
		return 0;
	case check_texture_addr4:
		cur_seq->unfinished = tex_address;
		tmp_addr = &cur_seq->t_addr[cur_seq->texture][9];
		*tmp_addr = (*tmp_addr & 0x00FFFFFF) | ((cmd & 0xFF) << 24);
		return 0;
	case check_texture_addr5:
	case check_texture_addr6:
		cur_seq->unfinished = tex_address;
		/*
		 * Texture width. We don't care since we have the pitch.
		 */
		return 0;
	case check_texture_addr7:
		cur_seq->unfinished = tex_address;
		tmp_addr = &(cur_seq->height[cur_seq->texture][0]);
		tmp_addr[5] = 1 << ((cmd & 0x00F00000) >> 20);
		tmp_addr[4] = 1 << ((cmd & 0x000F0000) >> 16);
		tmp_addr[3] = 1 << ((cmd & 0x0000F000) >> 12);
		tmp_addr[2] = 1 << ((cmd & 0x00000F00) >> 8);
		tmp_addr[1] = 1 << ((cmd & 0x000000F0) >> 4);
		tmp_addr[0] = 1 << (cmd & 0x0000000F);
		return 0;
	case check_texture_addr8:
		cur_seq->unfinished = tex_address;
		tmp_addr = &(cur_seq->height[cur_seq->texture][0]);
		tmp_addr[9] = 1 << ((cmd & 0x0000F000) >> 12);
		tmp_addr[8] = 1 << ((cmd & 0x00000F00) >> 8);
		tmp_addr[7] = 1 << ((cmd & 0x000000F0) >> 4);
		tmp_addr[6] = 1 << (cmd & 0x0000000F);
		return 0;
	case check_texture_addr_mode:
		cur_seq->unfinished = tex_address;
		if (2 == (tmp = cmd & 0x00000003)) {
			DRM_ERROR
			    ("Attempt to fetch texture from system memory.\n");
			return 2;
		}
		cur_seq->agp_texture = (tmp == 3);
		cur_seq->tex_palette_size[cur_seq->texture] =
		    (cmd >> 16) & 0x000000007;
		return 0;
	case check_for_vertex_count:
		cur_seq->vertex_count = cmd & 0x0000FFFF;
		return 0;
	case check_number_texunits:
		cur_seq->multitex = (cmd >> 3) & 1;
		return 0;
	default:
		DRM_ERROR("Illegal DMA data: 0x%x\n", cmd);
		return 2;
	}
	return 2;
}

static __inline__ int
via_check_prim_list(uint32_t const **buffer, const uint32_t * buf_end,
		    drm_via_state_t *cur_seq)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(cur_seq->dev);
	uint32_t a_fire, bcmd, dw_count;
	int ret = 0;
	int have_fire;
	const uint32_t *buf = *buffer;

	while (buf < buf_end) {
		have_fire = 0;
		if ((buf_end - buf) < 2) {
			DRM_ERROR
			    ("Unexpected termination of primitive list.\n");
			ret = 1;
			break;
		}
		if ((*buf & HC_ACMD_MASK) != HC_ACMD_HCmdB)
			break;
		bcmd = *buf++;
		if ((*buf & HC_ACMD_MASK) != HC_ACMD_HCmdA) {
			DRM_ERROR("Expected Vertex List A command, got 0x%x\n",
				  *buf);
			ret = 1;
			break;
		}
		a_fire =
		    *buf++ | HC_HPLEND_MASK | HC_HPMValidN_MASK |
		    HC_HE3Fire_MASK;

		/*
		 * How many dwords per vertex ?
		 */

		if (cur_seq->agp && ((bcmd & (0xF << 11)) == 0)) {
			DRM_ERROR("Illegal B command vertex data for AGP.\n");
			ret = 1;
			break;
		}

		dw_count = 0;
		if (bcmd & (1 << 7))
			dw_count += (cur_seq->multitex) ? 2 : 1;
		if (bcmd & (1 << 8))
			dw_count += (cur_seq->multitex) ? 2 : 1;
		if (bcmd & (1 << 9))
			dw_count++;
		if (bcmd & (1 << 10))
			dw_count++;
		if (bcmd & (1 << 11))
			dw_count++;
		if (bcmd & (1 << 12))
			dw_count++;
		if (bcmd & (1 << 13))
			dw_count++;
		if (bcmd & (1 << 14))
			dw_count++;

		while (buf < buf_end) {
			if (*buf == a_fire) {
				if (dev_priv->num_fire_offsets >=
				    VIA_FIRE_BUF_SIZE) {
					DRM_ERROR("Fire offset buffer full.\n");
					ret = 1;
					break;
				}
				dev_priv->fire_offsets[dev_priv->
						       num_fire_offsets++] =
				    buf;
				have_fire = 1;
				buf++;
				if (buf < buf_end && *buf == a_fire)
					buf++;
				break;
			}
			if ((*buf == HALCYON_HEADER2) ||
			    ((*buf & HALCYON_FIREMASK) == HALCYON_FIRECMD)) {
				DRM_ERROR("Missing Vertex Fire command, "
					  "Stray Vertex Fire command  or verifier "
					  "lost sync.\n");
				ret = 1;
				break;
			}
			if ((ret = eat_words(&buf, buf_end, dw_count)))
				break;
		}
		if (buf >= buf_end && !have_fire) {
			DRM_ERROR("Missing Vertex Fire command or verifier "
				  "lost sync.\n");
			ret = 1;
			break;
		}
		if (cur_seq->agp && ((buf - cur_seq->buf_start) & 0x01)) {
			DRM_ERROR("AGP Primitive list end misaligned.\n");
			ret = 1;
			break;
		}
	}
	*buffer = buf;
	return ret;
}

static __inline__ verifier_state_t
via_check_header2(uint32_t const **buffer, const uint32_t *buf_end,
		  drm_via_state_t *hc_state)
{
	uint32_t cmd;
	int hz_mode;
	hazard_t hz;
	const uint32_t *buf = *buffer;
	const hazard_t *hz_table;

	if ((buf_end - buf) < 2) {
		DRM_ERROR
		    ("Illegal termination of DMA HALCYON_HEADER2 sequence.\n");
		return state_error;
	}
	buf++;
	cmd = (*buf++ & 0xFFFF0000) >> 16;

	switch (cmd) {
	case HC_ParaType_CmdVdata:
		if (via_check_prim_list(&buf, buf_end, hc_state))
			return state_error;
		*buffer = buf;
		return state_command;
	case HC_ParaType_NotTex:
		hz_table = table1;
		break;
	case HC_ParaType_Tex:
		hc_state->texture = 0;
		hz_table = table2;
		break;
	case (HC_ParaType_Tex | (HC_SubType_Tex1 << 8)):
		hc_state->texture = 1;
		hz_table = table2;
		break;
	case (HC_ParaType_Tex | (HC_SubType_TexGeneral << 8)):
		hz_table = table3;
		break;
	case HC_ParaType_Auto:
		if (eat_words(&buf, buf_end, 2))
			return state_error;
		*buffer = buf;
		return state_command;
	case (HC_ParaType_Palette | (HC_SubType_Stipple << 8)):
		if (eat_words(&buf, buf_end, 32))
			return state_error;
		*buffer = buf;
		return state_command;
	case (HC_ParaType_Palette | (HC_SubType_TexPalette0 << 8)):
	case (HC_ParaType_Palette | (HC_SubType_TexPalette1 << 8)):
		DRM_ERROR("Texture palettes are rejected because of "
			  "lack of info how to determine their size.\n");
		return state_error;
	case (HC_ParaType_Palette | (HC_SubType_FogTable << 8)):
		DRM_ERROR("Fog factor palettes are rejected because of "
			  "lack of info how to determine their size.\n");
		return state_error;
	default:

		/*
		 * There are some unimplemented HC_ParaTypes here, that
		 * need to be implemented if the Mesa driver is extended.
		 */

		DRM_ERROR("Invalid or unimplemented HALCYON_HEADER2 "
			  "DMA subcommand: 0x%x. Previous dword: 0x%x\n",
			  cmd, *(buf - 2));
		*buffer = buf;
		return state_error;
	}

	while (buf < buf_end) {
		cmd = *buf++;
		if ((hz = hz_table[cmd >> 24])) {
			if ((hz_mode = investigate_hazard(cmd, hz, hc_state))) {
				if (hz_mode == 1) {
					buf--;
					break;
				}
				return state_error;
			}
		} else if (hc_state->unfinished &&
			   finish_current_sequence(hc_state)) {
			return state_error;
		}
	}
	if (hc_state->unfinished && finish_current_sequence(hc_state))
		return state_error;
	*buffer = buf;
	return state_command;
}

static __inline__ verifier_state_t
via_parse_header2(struct via_drm_priv *dev_priv, uint32_t const **buffer,
		  const uint32_t *buf_end, int *fire_count)
{
	uint32_t cmd;
	const uint32_t *buf = *buffer;
	const uint32_t *next_fire;
	int burst = 0;

	next_fire = dev_priv->fire_offsets[*fire_count];
	buf++;
	cmd = (*buf & 0xFFFF0000) >> 16;
	via_write(dev_priv, HC_REG_TRANS_SET + HC_REG_BASE, *buf++);
	switch (cmd) {
	case HC_ParaType_CmdVdata:
		while ((buf < buf_end) &&
		       (*fire_count < dev_priv->num_fire_offsets) &&
		       (*buf & HC_ACMD_MASK) == HC_ACMD_HCmdB) {
			while (buf <= next_fire) {
				via_write(dev_priv, HC_REG_TRANS_SPACE + HC_REG_BASE +
					  (burst & 63), *buf++);
				burst += 4;
			}
			if ((buf < buf_end)
			    && ((*buf & HALCYON_FIREMASK) == HALCYON_FIRECMD))
				buf++;

			if (++(*fire_count) < dev_priv->num_fire_offsets)
				next_fire = dev_priv->fire_offsets[*fire_count];
		}
		break;
	default:
		while (buf < buf_end) {

			if (*buf == HC_HEADER2 ||
			    (*buf & HALCYON_HEADER1MASK) == HALCYON_HEADER1 ||
			    (*buf & VIA_VIDEOMASK) == VIA_VIDEO_HEADER5 ||
			    (*buf & VIA_VIDEOMASK) == VIA_VIDEO_HEADER6)
				break;

			via_write(dev_priv, HC_REG_TRANS_SPACE + HC_REG_BASE +
				  (burst & 63), *buf++);
			burst += 4;
		}
	}
	*buffer = buf;
	return state_command;
}

static __inline__ int verify_mmio_address(uint32_t address)
{
	if ((address > 0x3FF) && (address < 0xC00)) {
		DRM_ERROR("Invalid VIDEO DMA command. "
			  "Attempt to access 3D- or command burst area.\n");
		return 1;
	} else if ((address > 0xCFF) && (address < 0x1300)) {
		DRM_ERROR("Invalid VIDEO DMA command. "
			  "Attempt to access PCI DMA area.\n");
		return 1;
	} else if (address > 0x13FF) {
		DRM_ERROR("Invalid VIDEO DMA command. "
			  "Attempt to access VGA registers.\n");
		return 1;
	}
	return 0;
}

static __inline__ int
verify_video_tail(uint32_t const **buffer, const uint32_t * buf_end,
		  uint32_t dwords)
{
	const uint32_t *buf = *buffer;

	if (buf_end - buf < dwords) {
		DRM_ERROR("Illegal termination of video command.\n");
		return 1;
	}
	while (dwords--) {
		if (*buf++) {
			DRM_ERROR("Illegal video command tail.\n");
			return 1;
		}
	}
	*buffer = buf;
	return 0;
}

static __inline__ verifier_state_t
via_check_header1(uint32_t const **buffer, const uint32_t * buf_end)
{
	uint32_t cmd;
	const uint32_t *buf = *buffer;
	verifier_state_t ret = state_command;

	while (buf < buf_end) {
		cmd = *buf;
		if ((cmd > ((0x3FF >> 2) | HALCYON_HEADER1)) &&
		    (cmd < ((0xC00 >> 2) | HALCYON_HEADER1))) {
			if ((cmd & HALCYON_HEADER1MASK) != HALCYON_HEADER1)
				break;
			DRM_ERROR("Invalid HALCYON_HEADER1 command. "
				  "Attempt to access 3D- or command burst area.\n");
			ret = state_error;
			break;
		} else if (cmd > ((0xCFF >> 2) | HALCYON_HEADER1)) {
			if ((cmd & HALCYON_HEADER1MASK) != HALCYON_HEADER1)
				break;
			DRM_ERROR("Invalid HALCYON_HEADER1 command. "
				  "Attempt to access VGA registers.\n");
			ret = state_error;
			break;
		} else {
			buf += 2;
		}
	}
	*buffer = buf;
	return ret;
}

static __inline__ verifier_state_t
via_parse_header1(struct via_drm_priv *dev_priv, uint32_t const **buffer,
		  const uint32_t *buf_end)
{
	register uint32_t cmd;
	const uint32_t *buf = *buffer;

	while (buf < buf_end) {
		cmd = *buf;
		if ((cmd & HALCYON_HEADER1MASK) != HALCYON_HEADER1)
			break;
		via_write(dev_priv, (cmd & ~HALCYON_HEADER1MASK) << 2, *++buf);
		buf++;
	}
	*buffer = buf;
	return state_command;
}

static __inline__ verifier_state_t
via_check_vheader5(uint32_t const **buffer, const uint32_t *buf_end)
{
	uint32_t data;
	const uint32_t *buf = *buffer;

	if (buf_end - buf < 4) {
		DRM_ERROR("Illegal termination of video header5 command\n");
		return state_error;
	}

	data = *buf++ & ~VIA_VIDEOMASK;
	if (verify_mmio_address(data))
		return state_error;

	data = *buf++;
	if (*buf++ != 0x00F50000) {
		DRM_ERROR("Illegal header5 header data\n");
		return state_error;
	}
	if (*buf++ != 0x00000000) {
		DRM_ERROR("Illegal header5 header data\n");
		return state_error;
	}
	if (eat_words(&buf, buf_end, data))
		return state_error;
	if ((data & 3) && verify_video_tail(&buf, buf_end, 4 - (data & 3)))
		return state_error;
	*buffer = buf;
	return state_command;

}

static __inline__ verifier_state_t
via_parse_vheader5(struct via_drm_priv *dev_priv, uint32_t const **buffer,
		   const uint32_t *buf_end)
{
	uint32_t addr, count, i;
	const uint32_t *buf = *buffer;

	addr = *buf++ & ~VIA_VIDEOMASK;
	i = count = *buf;
	buf += 3;
	while (i--)
		via_write(dev_priv, addr, *buf++);
	if (count & 3)
		buf += 4 - (count & 3);
	*buffer = buf;
	return state_command;
}

static __inline__ verifier_state_t
via_check_vheader6(uint32_t const **buffer, const uint32_t * buf_end)
{
	uint32_t data;
	const uint32_t *buf = *buffer;
	uint32_t i;

	if (buf_end - buf < 4) {
		DRM_ERROR("Illegal termination of video header6 command\n");
		return state_error;
	}
	buf++;
	data = *buf++;
	if (*buf++ != 0x00F60000) {
		DRM_ERROR("Illegal header6 header data\n");
		return state_error;
	}
	if (*buf++ != 0x00000000) {
		DRM_ERROR("Illegal header6 header data\n");
		return state_error;
	}
	if ((buf_end - buf) < (data << 1)) {
		DRM_ERROR("Illegal termination of video header6 command\n");
		return state_error;
	}
	for (i = 0; i < data; ++i) {
		if (verify_mmio_address(*buf++))
			return state_error;
		buf++;
	}
	data <<= 1;
	if ((data & 3) && verify_video_tail(&buf, buf_end, 4 - (data & 3)))
		return state_error;
	*buffer = buf;
	return state_command;
}

static __inline__ verifier_state_t
via_parse_vheader6(struct via_drm_priv *dev_priv, uint32_t const **buffer,
		   const uint32_t *buf_end)
{

	uint32_t addr, count, i;
	const uint32_t *buf = *buffer;

	i = count = *++buf;
	buf += 3;
	while (i--) {
		addr = *buf++;
		via_write(dev_priv, addr, *buf++);
	}
	count <<= 1;
	if (count & 3)
		buf += 4 - (count & 3);
	*buffer = buf;
	return state_command;
}

static int
via_verify_command_stream(const uint32_t * buf, unsigned int size,
			  struct drm_device * dev, int agp)
{

	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	drm_via_state_t *hc_state = &dev_priv->hc_state;
	drm_via_state_t saved_state = *hc_state;
	uint32_t cmd;
	const uint32_t *buf_end = buf + (size >> 2);
	verifier_state_t state = state_command;
	int cme_video;
	int supported_3d;

	cme_video = (dev_priv->chipset == VIA_PRO_GROUP_A ||
		     dev_priv->chipset == VIA_DX9_0);

	supported_3d = dev_priv->chipset != VIA_DX9_0;

	hc_state->dev = dev;
	hc_state->unfinished = no_sequence;
	hc_state->map_cache = NULL;
	hc_state->agp = agp;
	hc_state->buf_start = buf;
	dev_priv->num_fire_offsets = 0;

	while (buf < buf_end) {

		switch (state) {
		case state_header2:
			state = via_check_header2(&buf, buf_end, hc_state);
			break;
		case state_header1:
			state = via_check_header1(&buf, buf_end);
			break;
		case state_vheader5:
			state = via_check_vheader5(&buf, buf_end);
			break;
		case state_vheader6:
			state = via_check_vheader6(&buf, buf_end);
			break;
		case state_command:
			cmd = *buf;
			if ((cmd == HALCYON_HEADER2) && supported_3d)
				state = state_header2;
			else if ((cmd & HALCYON_HEADER1MASK) == HALCYON_HEADER1)
				state = state_header1;
			else if (cme_video
				 && (cmd & VIA_VIDEOMASK) == VIA_VIDEO_HEADER5)
				state = state_vheader5;
			else if (cme_video
				 && (cmd & VIA_VIDEOMASK) == VIA_VIDEO_HEADER6)
				state = state_vheader6;
			else if ((cmd == HALCYON_HEADER2) && !supported_3d) {
				DRM_ERROR("Accelerated 3D is not supported on this chipset yet.\n");
				state = state_error;
			} else {
				DRM_ERROR
				    ("Invalid / Unimplemented DMA HEADER command. 0x%x\n",
				     cmd);
				state = state_error;
			}
			break;
		case state_error:
		default:
			*hc_state = saved_state;
			return -EINVAL;
		}
	}
	if (state == state_error) {
		*hc_state = saved_state;
		return -EINVAL;
	}
	return 0;
}

static int
via_parse_command_stream(struct drm_device *dev, const uint32_t *buf,
			 unsigned int size)
{

	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	uint32_t cmd;
	const uint32_t *buf_end = buf + (size >> 2);
	verifier_state_t state = state_command;
	int fire_count = 0;

	while (buf < buf_end) {

		switch (state) {
		case state_header2:
			state =
			    via_parse_header2(dev_priv, &buf, buf_end,
					      &fire_count);
			break;
		case state_header1:
			state = via_parse_header1(dev_priv, &buf, buf_end);
			break;
		case state_vheader5:
			state = via_parse_vheader5(dev_priv, &buf, buf_end);
			break;
		case state_vheader6:
			state = via_parse_vheader6(dev_priv, &buf, buf_end);
			break;
		case state_command:
			cmd = *buf;
			if (cmd == HALCYON_HEADER2)
				state = state_header2;
			else if ((cmd & HALCYON_HEADER1MASK) == HALCYON_HEADER1)
				state = state_header1;
			else if ((cmd & VIA_VIDEOMASK) == VIA_VIDEO_HEADER5)
				state = state_vheader5;
			else if ((cmd & VIA_VIDEOMASK) == VIA_VIDEO_HEADER6)
				state = state_vheader6;
			else {
				DRM_ERROR
				    ("Invalid / Unimplemented DMA HEADER command. 0x%x\n",
				     cmd);
				state = state_error;
			}
			break;
		case state_error:
		default:
			return -EINVAL;
		}
	}
	if (state == state_error)
		return -EINVAL;
	return 0;
}

static void
setup_hazard_table(hz_init_t init_table[], hazard_t table[], int size)
{
	int i;

	for (i = 0; i < 256; ++i)
		table[i] = forbidden_command;

	for (i = 0; i < size; ++i)
		table[init_table[i].code] = init_table[i].hz;
}

static void via_init_command_verifier(void)
{
	setup_hazard_table(init_table1, table1, ARRAY_SIZE(init_table1));
	setup_hazard_table(init_table2, table2, ARRAY_SIZE(init_table2));
	setup_hazard_table(init_table3, table3, ARRAY_SIZE(init_table3));
}

static void via_cmdbuf_start(struct via_drm_priv *dev_priv);
static void via_cmdbuf_pause(struct via_drm_priv *dev_priv);
static void via_cmdbuf_reset(struct via_drm_priv *dev_priv);
static void via_cmdbuf_rewind(struct via_drm_priv *dev_priv);
static int via_wait_idle(struct via_drm_priv *dev_priv);
static void via_pad_cache(struct via_drm_priv *dev_priv, int qwords);

/*
 * Free space in command buffer.
 */

static uint32_t via_cmdbuf_space(struct via_drm_priv *dev_priv)
{
	uint32_t agp_base = dev_priv->dma_offset + (uint32_t) dev_priv->agpAddr;
	uint32_t hw_addr = *(dev_priv->hw_addr_ptr) - agp_base;

	return ((hw_addr <= dev_priv->dma_low) ?
		(dev_priv->dma_high + hw_addr - dev_priv->dma_low) :
		(hw_addr - dev_priv->dma_low));
}

/*
 * How much does the command regulator lag behind?
 */

static uint32_t via_cmdbuf_lag(struct via_drm_priv *dev_priv)
{
	uint32_t agp_base = dev_priv->dma_offset + (uint32_t) dev_priv->agpAddr;
	uint32_t hw_addr = *(dev_priv->hw_addr_ptr) - agp_base;

	return ((hw_addr <= dev_priv->dma_low) ?
		(dev_priv->dma_low - hw_addr) :
		(dev_priv->dma_wrap + dev_priv->dma_low - hw_addr));
}

/*
 * Check that the given size fits in the buffer, otherwise wait.
 */

static inline int
via_cmdbuf_wait(struct via_drm_priv *dev_priv, unsigned int size)
{
	uint32_t agp_base = dev_priv->dma_offset + (uint32_t) dev_priv->agpAddr;
	uint32_t cur_addr, hw_addr, next_addr;
	volatile uint32_t *hw_addr_ptr;
	uint32_t count;
	hw_addr_ptr = dev_priv->hw_addr_ptr;
	cur_addr = dev_priv->dma_low;
	next_addr = cur_addr + size + 512 * 1024;
	count = 1000000;
	do {
		hw_addr = *hw_addr_ptr - agp_base;
		if (count-- == 0) {
			DRM_ERROR
			    ("via_cmdbuf_wait timed out hw %x cur_addr %x next_addr %x\n",
			     hw_addr, cur_addr, next_addr);
			return -1;
		}
		if  ((cur_addr < hw_addr) && (next_addr >= hw_addr))
			msleep(1);
	} while ((cur_addr < hw_addr) && (next_addr >= hw_addr));
	return 0;
}

/*
 * Checks whether buffer head has reach the end. Rewind the ring buffer
 * when necessary.
 *
 * Returns virtual pointer to ring buffer.
 */

static inline uint32_t *via_check_dma(struct via_drm_priv * dev_priv,
				      unsigned int size)
{
	if ((dev_priv->dma_low + size + 4 * CMDBUF_ALIGNMENT_SIZE) >
	    dev_priv->dma_high) {
		via_cmdbuf_rewind(dev_priv);
	}
	if (via_cmdbuf_wait(dev_priv, size) != 0)
		return NULL;

	return (uint32_t *) (dev_priv->dma_ptr + dev_priv->dma_low);
}

static int via_dma_cleanup(struct drm_device *dev)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	if (dev_priv) {
		if (dev_priv->ring.virtual_start && dev_priv->mmio) {
			via_cmdbuf_reset(dev_priv);

			drm_legacy_ioremapfree(&dev_priv->ring.map, dev);
			dev_priv->ring.virtual_start = NULL;
		}
	}

	return 0;
}

static int via_initialize(struct drm_device *dev,
			  struct via_drm_priv *dev_priv,
			  drm_via_dma_init_t *init)
{
	if (!dev_priv || !dev_priv->mmio) {
		DRM_ERROR("via_dma_init called before via_map_init\n");
		return -EFAULT;
	}

	if (dev_priv->ring.virtual_start != NULL) {
		DRM_ERROR("called again without calling cleanup\n");
		return -EFAULT;
	}

	if (!dev->agp || !dev->agp->base) {
		DRM_ERROR("called with no agp memory available\n");
		return -EFAULT;
	}

	if (dev_priv->chipset == VIA_DX9_0) {
		DRM_ERROR("AGP DMA is not supported on this chip\n");
		return -EINVAL;
	}

	dev_priv->ring.map.offset = dev->agp->base + init->offset;
	dev_priv->ring.map.size = init->size;
	dev_priv->ring.map.type = 0;
	dev_priv->ring.map.flags = 0;
	dev_priv->ring.map.mtrr = 0;

	drm_legacy_ioremap(&dev_priv->ring.map, dev);

	if (dev_priv->ring.map.handle == NULL) {
		via_dma_cleanup(dev);
		DRM_ERROR("can not ioremap virtual address for"
			  " ring buffer\n");
		return -ENOMEM;
	}

	dev_priv->ring.virtual_start = dev_priv->ring.map.handle;

	dev_priv->dma_ptr = dev_priv->ring.virtual_start;
	dev_priv->dma_low = 0;
	dev_priv->dma_high = init->size;
	dev_priv->dma_wrap = init->size;
	dev_priv->dma_offset = init->offset;
	dev_priv->agpAddr = dev->agp->base;
	dev_priv->last_pause_ptr = NULL;
	dev_priv->hw_addr_ptr =
		(volatile uint32_t *)((char *)dev_priv->mmio +
		init->reg_pause_addr);

	via_cmdbuf_start(dev_priv);

	return 0;
}

int via_dma_init_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	drm_via_dma_init_t *init = data;
	int retcode = 0;

	switch (init->func) {
	case VIA_INIT_DMA:
		if (!capable(CAP_SYS_ADMIN))
			retcode = -EPERM;
		else
			retcode = via_initialize(dev, dev_priv, init);
		break;
	case VIA_CLEANUP_DMA:
		if (!capable(CAP_SYS_ADMIN))
			retcode = -EPERM;
		else
			retcode = via_dma_cleanup(dev);
		break;
	case VIA_DMA_INITIALIZED:
		retcode = (dev_priv->ring.virtual_start != NULL) ?
			0 : -EFAULT;
		break;
	default:
		retcode = -EINVAL;
		break;
	}

	return retcode;
}

static int via_dispatch_cmdbuffer(struct drm_device *dev, drm_via_cmdbuffer_t *cmd)
{
	struct via_drm_priv *dev_priv;
	uint32_t *vb;
	int ret;

	dev_priv = to_via_drm_priv(dev);

	if (dev_priv->ring.virtual_start == NULL) {
		DRM_ERROR("called without initializing AGP ring buffer.\n");
		return -EFAULT;
	}

	if (cmd->size > VIA_PCI_BUF_SIZE)
		return -ENOMEM;

	if (copy_from_user(dev_priv->pci_buf, cmd->buf, cmd->size))
		return -EFAULT;

	/*
	 * Running this function on AGP memory is dead slow. Therefore
	 * we run it on a temporary cacheable system memory buffer and
	 * copy it to AGP memory when ready.
	 */

	if ((ret =
	     via_verify_command_stream((uint32_t *) dev_priv->pci_buf,
				       cmd->size, dev, 1))) {
		return ret;
	}

	vb = via_check_dma(dev_priv, (cmd->size < 0x100) ? 0x102 : cmd->size);
	if (vb == NULL)
		return -EAGAIN;

	memcpy(vb, dev_priv->pci_buf, cmd->size);

	dev_priv->dma_low += cmd->size;

	/*
	 * Small submissions somehow stalls the CPU. (AGP cache effects?)
	 * pad to greater size.
	 */

	if (cmd->size < 0x100)
		via_pad_cache(dev_priv, (0x100 - cmd->size) >> 3);
	via_cmdbuf_pause(dev_priv);

	return 0;
}

int via_driver_dma_quiescent(struct drm_device *dev)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	if (!via_wait_idle(dev_priv))
		return -EBUSY;
	return 0;
}

int via_flush_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	int ret;

	mutex_lock(&dev_priv->ring_lock);
	ret = via_driver_dma_quiescent(dev);
	mutex_unlock(&dev_priv->ring_lock);

	return ret;
}

int via_cmdbuffer_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file_priv)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	drm_via_cmdbuffer_t *cmdbuf = data;
	int ret;

	mutex_lock(&dev_priv->ring_lock);

	DRM_DEBUG("buf %p size %lu\n", cmdbuf->buf, cmdbuf->size);

	ret = via_dispatch_cmdbuffer(dev, cmdbuf);

	mutex_unlock(&dev_priv->ring_lock);
	return ret;
}

static int via_dispatch_pci_cmdbuffer(struct drm_device *dev,
				      drm_via_cmdbuffer_t *cmd)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	int ret;

	if (cmd->size > VIA_PCI_BUF_SIZE)
		return -ENOMEM;
	if (copy_from_user(dev_priv->pci_buf, cmd->buf, cmd->size))
		return -EFAULT;

	if ((ret =
	     via_verify_command_stream((uint32_t *) dev_priv->pci_buf,
				       cmd->size, dev, 0))) {
		return ret;
	}

	ret =
	    via_parse_command_stream(dev, (const uint32_t *)dev_priv->pci_buf,
				     cmd->size);
	return ret;
}

int via_pci_cmdbuffer_ioctl(struct drm_device *dev, void *data,
				   struct drm_file *file_priv)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	drm_via_cmdbuffer_t *cmdbuf = data;
	int ret;

	mutex_lock(&dev_priv->ring_lock);

	DRM_DEBUG("buf %p size %lu\n", cmdbuf->buf, cmdbuf->size);

	ret = via_dispatch_pci_cmdbuffer(dev, cmdbuf);

	mutex_unlock(&dev_priv->ring_lock);
	return ret;
}

static inline uint32_t *via_align_buffer(struct via_drm_priv *dev_priv,
					 uint32_t * vb, int qw_count)
{
	for (; qw_count > 0; --qw_count)
		VIA_OUT_RING_QW(HC_DUMMY, HC_DUMMY);
	return vb;
}

/*
 * This function is used internally by ring buffer management code.
 *
 * Returns virtual pointer to ring buffer.
 */
static inline uint32_t *via_get_dma(struct via_drm_priv *dev_priv)
{
	return (uint32_t *) (dev_priv->dma_ptr + dev_priv->dma_low);
}

/*
 * Hooks a segment of data into the tail of the ring-buffer by
 * modifying the pause address stored in the buffer itself. If
 * the regulator has already paused, restart it.
 */
static int via_hook_segment(struct via_drm_priv *dev_priv,
			    uint32_t pause_addr_hi, uint32_t pause_addr_lo,
			    int no_pci_fire)
{
	int paused, count;
	volatile uint32_t *paused_at = dev_priv->last_pause_ptr;
	uint32_t reader, ptr;
	uint32_t diff;

	paused = 0;
	via_flush_write_combine();
	(void) *(volatile uint32_t *)(via_get_dma(dev_priv) - 1);

	*paused_at = pause_addr_lo;
	via_flush_write_combine();
	(void) *paused_at;

	reader = *(dev_priv->hw_addr_ptr);
	ptr = ((volatile char *)paused_at - dev_priv->dma_ptr) +
		dev_priv->dma_offset + (uint32_t) dev_priv->agpAddr + 4;

	dev_priv->last_pause_ptr = via_get_dma(dev_priv) - 1;

	/*
	 * If there is a possibility that the command reader will
	 * miss the new pause address and pause on the old one,
	 * In that case we need to program the new start address
	 * using PCI.
	 */

	diff = (uint32_t) (ptr - reader) - dev_priv->dma_diff;
	count = 10000000;
	while (diff == 0 && count--) {
		paused = (via_read(dev_priv, 0x41c) & 0x80000000);
		if (paused)
			break;
		reader = *(dev_priv->hw_addr_ptr);
		diff = (uint32_t) (ptr - reader) - dev_priv->dma_diff;
	}

	paused = via_read(dev_priv, 0x41c) & 0x80000000;

	if (paused && !no_pci_fire) {
		reader = *(dev_priv->hw_addr_ptr);
		diff = (uint32_t) (ptr - reader) - dev_priv->dma_diff;
		diff &= (dev_priv->dma_high - 1);
		if (diff != 0 && diff < (dev_priv->dma_high >> 1)) {
			DRM_ERROR("Paused at incorrect address. "
				  "0x%08x, 0x%08x 0x%08x\n",
				  ptr, reader, dev_priv->dma_diff);
		} else if (diff == 0) {
			/*
			 * There is a concern that these writes may stall the PCI bus
			 * if the GPU is not idle. However, idling the GPU first
			 * doesn't make a difference.
			 */

			via_write(dev_priv, VIA_REG_TRANSET, (HC_ParaType_PreCR << 16));
			via_write(dev_priv, VIA_REG_TRANSPACE, pause_addr_hi);
			via_write(dev_priv, VIA_REG_TRANSPACE, pause_addr_lo);
			via_read(dev_priv, VIA_REG_TRANSPACE);
		}
	}
	return paused;
}

static int via_wait_idle(struct via_drm_priv *dev_priv)
{
	int count = 10000000;

	while (!(via_read(dev_priv, VIA_REG_STATUS) & VIA_VR_QUEUE_BUSY) && --count)
		;

	while (count && (via_read(dev_priv, VIA_REG_STATUS) &
			   (VIA_CMD_RGTR_BUSY | VIA_2D_ENG_BUSY |
			    VIA_3D_ENG_BUSY)))
		--count;
	return count;
}

static uint32_t *via_align_cmd(struct via_drm_priv *dev_priv, uint32_t cmd_type,
			       uint32_t addr, uint32_t *cmd_addr_hi,
			       uint32_t *cmd_addr_lo, int skip_wait)
{
	uint32_t agp_base;
	uint32_t cmd_addr, addr_lo, addr_hi;
	uint32_t *vb;
	uint32_t qw_pad_count;

	if (!skip_wait)
		via_cmdbuf_wait(dev_priv, 2 * CMDBUF_ALIGNMENT_SIZE);

	vb = via_get_dma(dev_priv);
	VIA_OUT_RING_QW(HC_HEADER2 | ((VIA_REG_TRANSET >> 2) << 12) |
			(VIA_REG_TRANSPACE >> 2), HC_ParaType_PreCR << 16);
	agp_base = dev_priv->dma_offset + (uint32_t) dev_priv->agpAddr;
	qw_pad_count = (CMDBUF_ALIGNMENT_SIZE >> 3) -
	    ((dev_priv->dma_low & CMDBUF_ALIGNMENT_MASK) >> 3);

	cmd_addr = (addr) ? addr :
	    agp_base + dev_priv->dma_low - 8 + (qw_pad_count << 3);
	addr_lo = ((HC_SubA_HAGPBpL << 24) | (cmd_type & HC_HAGPBpID_MASK) |
		   (cmd_addr & HC_HAGPBpL_MASK));
	addr_hi = ((HC_SubA_HAGPBpH << 24) | (cmd_addr >> 24));

	vb = via_align_buffer(dev_priv, vb, qw_pad_count - 1);
	VIA_OUT_RING_QW(*cmd_addr_hi = addr_hi, *cmd_addr_lo = addr_lo);
	return vb;
}

static void via_cmdbuf_start(struct via_drm_priv *dev_priv)
{
	uint32_t pause_addr_lo, pause_addr_hi;
	uint32_t start_addr, start_addr_lo;
	uint32_t end_addr, end_addr_lo;
	uint32_t command;
	uint32_t agp_base;
	uint32_t ptr;
	uint32_t reader;
	int count;

	dev_priv->dma_low = 0;

	agp_base = dev_priv->dma_offset + (uint32_t) dev_priv->agpAddr;
	start_addr = agp_base;
	end_addr = agp_base + dev_priv->dma_high;

	start_addr_lo = ((HC_SubA_HAGPBstL << 24) | (start_addr & 0xFFFFFF));
	end_addr_lo = ((HC_SubA_HAGPBendL << 24) | (end_addr & 0xFFFFFF));
	command = ((HC_SubA_HAGPCMNT << 24) | (start_addr >> 24) |
		   ((end_addr & 0xff000000) >> 16));

	dev_priv->last_pause_ptr =
	    via_align_cmd(dev_priv, HC_HAGPBpID_PAUSE, 0,
			  &pause_addr_hi, &pause_addr_lo, 1) - 1;

	via_flush_write_combine();
	(void) *(volatile uint32_t *)dev_priv->last_pause_ptr;

	via_write(dev_priv, VIA_REG_TRANSET, (HC_ParaType_PreCR << 16));
	via_write(dev_priv, VIA_REG_TRANSPACE, command);
	via_write(dev_priv, VIA_REG_TRANSPACE, start_addr_lo);
	via_write(dev_priv, VIA_REG_TRANSPACE, end_addr_lo);

	via_write(dev_priv, VIA_REG_TRANSPACE, pause_addr_hi);
	via_write(dev_priv, VIA_REG_TRANSPACE, pause_addr_lo);
	wmb();
	via_write(dev_priv, VIA_REG_TRANSPACE, command | HC_HAGPCMNT_MASK);
	via_read(dev_priv, VIA_REG_TRANSPACE);

	dev_priv->dma_diff = 0;

	count = 10000000;
	while (!(via_read(dev_priv, 0x41c) & 0x80000000) && count--);

	reader = *(dev_priv->hw_addr_ptr);
	ptr = ((volatile char *)dev_priv->last_pause_ptr - dev_priv->dma_ptr) +
	    dev_priv->dma_offset + (uint32_t) dev_priv->agpAddr + 4;

	/*
	 * This is the difference between where we tell the
	 * command reader to pause and where it actually pauses.
	 * This differs between hw implementation so we need to
	 * detect it.
	 */

	dev_priv->dma_diff = ptr - reader;
}

static void via_pad_cache(struct via_drm_priv *dev_priv, int qwords)
{
	uint32_t *vb;

	via_cmdbuf_wait(dev_priv, qwords + 2);
	vb = via_get_dma(dev_priv);
	VIA_OUT_RING_QW(HC_HEADER2, HC_ParaType_NotTex << 16);
	via_align_buffer(dev_priv, vb, qwords);
}

static inline void via_dummy_bitblt(struct via_drm_priv *dev_priv)
{
	uint32_t *vb = via_get_dma(dev_priv);
	SetReg2DAGP(0x0C, (0 | (0 << 16)));
	SetReg2DAGP(0x10, 0 | (0 << 16));
	SetReg2DAGP(0x0, 0x1 | 0x2000 | 0xAA000000);
}

static void via_cmdbuf_jump(struct via_drm_priv *dev_priv)
{
	uint32_t pause_addr_lo, pause_addr_hi;
	uint32_t jump_addr_lo, jump_addr_hi;
	volatile uint32_t *last_pause_ptr;
	uint32_t dma_low_save1, dma_low_save2;

	via_align_cmd(dev_priv, HC_HAGPBpID_JUMP, 0, &jump_addr_hi,
		      &jump_addr_lo, 0);

	dev_priv->dma_wrap = dev_priv->dma_low;

	/*
	 * Wrap command buffer to the beginning.
	 */

	dev_priv->dma_low = 0;
	if (via_cmdbuf_wait(dev_priv, CMDBUF_ALIGNMENT_SIZE) != 0)
		DRM_ERROR("via_cmdbuf_jump failed\n");

	via_dummy_bitblt(dev_priv);
	via_dummy_bitblt(dev_priv);

	last_pause_ptr =
	    via_align_cmd(dev_priv, HC_HAGPBpID_PAUSE, 0, &pause_addr_hi,
			  &pause_addr_lo, 0) - 1;
	via_align_cmd(dev_priv, HC_HAGPBpID_PAUSE, 0, &pause_addr_hi,
		      &pause_addr_lo, 0);

	*last_pause_ptr = pause_addr_lo;
	dma_low_save1 = dev_priv->dma_low;

	/*
	 * Now, set a trap that will pause the regulator if it tries to rerun the old
	 * command buffer. (Which may happen if via_hook_segment detects a command
	 * regulator pause and reissues the jump command over PCI, while the regulator
	 * has already taken the jump and actually paused at the current buffer end).
	 * There appears to be no other way to detect this condition, since the
	 * hw_addr_pointer does not seem to get updated immediately when a jump occurs.
	 */

	last_pause_ptr =
		via_align_cmd(dev_priv, HC_HAGPBpID_PAUSE, 0, &pause_addr_hi,
			      &pause_addr_lo, 0) - 1;
	via_align_cmd(dev_priv, HC_HAGPBpID_PAUSE, 0, &pause_addr_hi,
		      &pause_addr_lo, 0);
	*last_pause_ptr = pause_addr_lo;

	dma_low_save2 = dev_priv->dma_low;
	dev_priv->dma_low = dma_low_save1;
	via_hook_segment(dev_priv, jump_addr_hi, jump_addr_lo, 0);
	dev_priv->dma_low = dma_low_save2;
	via_hook_segment(dev_priv, pause_addr_hi, pause_addr_lo, 0);
}


static void via_cmdbuf_rewind(struct via_drm_priv *dev_priv)
{
	via_cmdbuf_jump(dev_priv);
}

static void via_cmdbuf_flush(struct via_drm_priv *dev_priv, uint32_t cmd_type)
{
	uint32_t pause_addr_lo, pause_addr_hi;

	via_align_cmd(dev_priv, cmd_type, 0, &pause_addr_hi, &pause_addr_lo, 0);
	via_hook_segment(dev_priv, pause_addr_hi, pause_addr_lo, 0);
}

static void via_cmdbuf_pause(struct via_drm_priv *dev_priv)
{
	via_cmdbuf_flush(dev_priv, HC_HAGPBpID_PAUSE);
}

static void via_cmdbuf_reset(struct via_drm_priv *dev_priv)
{
	via_cmdbuf_flush(dev_priv, HC_HAGPBpID_STOP);
	via_wait_idle(dev_priv);
}

/*
 * User interface to the space and lag functions.
 */

int via_cmdbuf_size_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv)
{
	struct via_drm_priv *dev_priv;
	drm_via_cmdbuf_size_t *d_siz = data;
	int ret = 0;
	uint32_t tmp_size, count;

	DRM_DEBUG("\n");

	dev_priv = to_via_drm_priv(dev);

	mutex_lock(&dev_priv->ring_lock);

	if (dev_priv->ring.virtual_start == NULL) {
		DRM_ERROR("called without initializing AGP ring buffer.\n");
		mutex_unlock(&dev_priv->ring_lock);
		return -EFAULT;
	}

	count = 1000000;
	tmp_size = d_siz->size;
	switch (d_siz->func) {
	case VIA_CMDBUF_SPACE:
		while (((tmp_size = via_cmdbuf_space(dev_priv)) < d_siz->size)
		       && --count) {
			if (!d_siz->wait)
				break;
		}
		if (!count) {
			DRM_ERROR("VIA_CMDBUF_SPACE timed out.\n");
			ret = -EAGAIN;
		}
		break;
	case VIA_CMDBUF_LAG:
		while (((tmp_size = via_cmdbuf_lag(dev_priv)) > d_siz->size)
		       && --count) {
			if (!d_siz->wait)
				break;
		}
		if (!count) {
			DRM_ERROR("VIA_CMDBUF_LAG timed out.\n");
			ret = -EAGAIN;
		}
		break;
	default:
		ret = -EFAULT;
	}
	d_siz->size = tmp_size;

	mutex_unlock(&dev_priv->ring_lock);

	return ret;
}

/*
 * IRQ and vblank support.
 */

static void viadrv_acknowledge_irqs(struct via_drm_priv *dev_priv)
{
	u32 status;

	if (dev_priv) {
		/* Acknowledge interrupts */
		status = via_read(dev_priv, VIA_REG_INTERRUPT);
		via_write(dev_priv, VIA_REG_INTERRUPT, status |
			  dev_priv->irq_pending_mask);
	}
}

static void via_ring_irq_preinstall(struct drm_device *dev)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	u32 status;
	drm_via_irq_t *cur_irq;
	int i;

	DRM_DEBUG("dev_priv: %p\n", dev_priv);
	if (dev_priv) {
		cur_irq = dev_priv->via_irqs;

		dev_priv->irq_enable_mask = VIA_IRQ_VBLANK_ENABLE;
		dev_priv->irq_pending_mask = VIA_IRQ_VBLANK_PENDING;

		if (dev_priv->chipset == VIA_PRO_GROUP_A ||
		    dev_priv->chipset == VIA_DX9_0) {
			dev_priv->irq_masks = via_pro_group_a_irqs;
			dev_priv->num_irqs = via_num_pro_group_a;
			dev_priv->irq_map = via_irqmap_pro_group_a;
		} else {
			dev_priv->irq_masks = via_unichrome_irqs;
			dev_priv->num_irqs = via_num_unichrome;
			dev_priv->irq_map = via_irqmap_unichrome;
		}

		for (i = 0; i < dev_priv->num_irqs; ++i) {
			atomic_set(&cur_irq->irq_received, 0);
			cur_irq->enable_mask = dev_priv->irq_masks[i][0];
			cur_irq->pending_mask = dev_priv->irq_masks[i][1];
			init_waitqueue_head(&cur_irq->irq_queue);
			dev_priv->irq_enable_mask |= cur_irq->enable_mask;
			dev_priv->irq_pending_mask |= cur_irq->pending_mask;
			cur_irq++;

			DRM_DEBUG("Initializing IRQ %d\n", i);
		}

		dev_priv->last_vblank_valid = 0;
		atomic_set(&dev_priv->vbl_received, 0);

		/* Clear VSync interrupt regs */
		status = via_read(dev_priv, VIA_REG_INTERRUPT);
		via_write(dev_priv, VIA_REG_INTERRUPT, status &
			  ~(dev_priv->irq_enable_mask));

		/* Clear bits if they're already high */
		viadrv_acknowledge_irqs(dev_priv);
	}
}

static int via_ring_irq_postinstall(struct drm_device *dev)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	u32 status;

	DRM_DEBUG("fun: %s\n", __func__);
	if (!dev_priv)
		return -EINVAL;

	status = via_read(dev_priv, VIA_REG_INTERRUPT);
	via_write(dev_priv, VIA_REG_INTERRUPT, status | VIA_IRQ_GLOBAL
		  | dev_priv->irq_enable_mask);

	/* Some magic, oh for some data sheets ! */
	via_write8(dev_priv, 0x83d4, 0x11);
	via_write8_mask(dev_priv, 0x83d5, 0x30, 0x30);

	return 0;
}

static void via_ring_irq_uninstall(struct drm_device *dev)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	u32 status;

	DRM_DEBUG("\n");
	if (dev_priv) {

		/* Some more magic, oh for some data sheets ! */

		via_write8(dev_priv, 0x83d4, 0x11);
		via_write8_mask(dev_priv, 0x83d5, 0x30, 0);

		status = via_read(dev_priv, VIA_REG_INTERRUPT);
		via_write(dev_priv, VIA_REG_INTERRUPT, status &
			  ~(VIA_IRQ_VBLANK_ENABLE | dev_priv->irq_enable_mask));
	}
}

irqreturn_t via_ring_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = (struct drm_device *)arg;
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	u32 status;
	int handled = 0;
	ktime_t cur_vblank;
	drm_via_irq_t *cur_irq = dev_priv->via_irqs;
	int i;

	status = via_read(dev_priv, VIA_REG_INTERRUPT);
	if (status & VIA_IRQ_VBLANK_PENDING) {
		atomic_inc(&dev_priv->vbl_received);
		if (!(atomic_read(&dev_priv->vbl_received) & 0x0F)) {
			cur_vblank = ktime_get();
			if (dev_priv->last_vblank_valid) {
				dev_priv->nsec_per_vblank =
					ktime_sub(cur_vblank,
						dev_priv->last_vblank) >> 4;
			}
			dev_priv->last_vblank = cur_vblank;
			dev_priv->last_vblank_valid = 1;
		}
		drm_handle_vblank(dev, 0);
		handled = 1;
	}

	for (i = 0; i < dev_priv->num_irqs; ++i) {
		if (status & cur_irq->pending_mask) {
			atomic_inc(&cur_irq->irq_received);
			wake_up(&cur_irq->irq_queue);
			handled = 1;
		}
		cur_irq++;
	}

	/* Acknowledge interrupts */
	via_write(dev_priv, VIA_REG_INTERRUPT, status);

	if (handled)
		return IRQ_HANDLED;
	else
		return IRQ_NONE;
}

static int
via_driver_irq_wait(struct drm_device *dev, unsigned int irq,
		    int force_sequence, unsigned int *sequence)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	unsigned int cur_irq_sequence;
	drm_via_irq_t *cur_irq;
	int ret = 0;
	maskarray_t *masks;
	int real_irq;

	DRM_DEBUG("\n");

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	if (irq >= drm_via_irq_num) {
		DRM_ERROR("Trying to wait on unknown irq %d\n", irq);
		return -EINVAL;
	}

	real_irq = dev_priv->irq_map[irq];

	if (real_irq < 0) {
		DRM_ERROR("Video IRQ %d not available on this hardware.\n",
			  irq);
		return -EINVAL;
	}

	masks = dev_priv->irq_masks;
	cur_irq = dev_priv->via_irqs + real_irq;

	if (masks[real_irq][2] && !force_sequence) {
		VIA_WAIT_ON(ret, cur_irq->irq_queue, 3 * HZ,
			    ((via_read(dev_priv, masks[irq][2]) & masks[irq][3]) ==
			     masks[irq][4]));
		cur_irq_sequence = atomic_read(&cur_irq->irq_received);
	} else {
		VIA_WAIT_ON(ret, cur_irq->irq_queue, 3 * HZ,
			    (((cur_irq_sequence =
			       atomic_read(&cur_irq->irq_received)) -
			      *sequence) <= (1 << 23)));
	}
	*sequence = cur_irq_sequence;
	return ret;
}

int via_wait_irq_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	drm_via_irqwait_t *irqwait = data;
	struct timespec64 now;
	int ret = 0;
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	drm_via_irq_t *cur_irq = dev_priv->via_irqs;
	int force_sequence;

	if (irqwait->request.irq >= dev_priv->num_irqs) {
		DRM_ERROR("Trying to wait on unknown irq %d\n",
			  irqwait->request.irq);
		return -EINVAL;
	}

	cur_irq += irqwait->request.irq;

	switch (irqwait->request.type & ~VIA_IRQ_FLAGS_MASK) {
	case VIA_IRQ_RELATIVE:
		irqwait->request.sequence +=
			atomic_read(&cur_irq->irq_received);
		irqwait->request.type &= ~_DRM_VBLANK_RELATIVE;
		break;
	case VIA_IRQ_ABSOLUTE:
		break;
	default:
		return -EINVAL;
	}

	if (irqwait->request.type & VIA_IRQ_SIGNAL) {
		DRM_ERROR("Signals on Via IRQs not implemented yet.\n");
		return -EINVAL;
	}

	force_sequence = (irqwait->request.type & VIA_IRQ_FORCE_SEQUENCE);

	ret = via_driver_irq_wait(dev, irqwait->request.irq, force_sequence,
				  &irqwait->request.sequence);
	ktime_get_ts64(&now);
	irqwait->reply.tval_sec = now.tv_sec;
	irqwait->reply.tval_usec = now.tv_nsec / NSEC_PER_USEC;

	return ret;
}

u32 via_get_vblank_counter(struct drm_device *dev, unsigned int pipe)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	if (pipe != 0)
		return 0;

	return atomic_read(&dev_priv->vbl_received);
}

int via_enable_vblank(struct drm_device *dev, unsigned int pipe)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	u32 status;

	if (pipe != 0) {
		DRM_ERROR("%s:  bad crtc %u\n", __func__, pipe);
		return -EINVAL;
	}

	status = via_read(dev_priv, VIA_REG_INTERRUPT);
	via_write(dev_priv, VIA_REG_INTERRUPT, status | VIA_IRQ_VBLANK_ENABLE);

	via_write8(dev_priv, 0x83d4, 0x11);
	via_write8_mask(dev_priv, 0x83d5, 0x30, 0x30);

	return 0;
}

void via_disable_vblank(struct drm_device *dev, unsigned int pipe)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	u32 status;

	status = via_read(dev_priv, VIA_REG_INTERRUPT);
	via_write(dev_priv, VIA_REG_INTERRUPT, status & ~VIA_IRQ_VBLANK_ENABLE);

	via_write8(dev_priv, 0x83d4, 0x11);
	via_write8_mask(dev_priv, 0x83d5, 0x30, 0);

	if (pipe != 0)
		DRM_ERROR("%s:  bad crtc %u\n", __func__, pipe);
}

/*
 * Legacy memory managers used by VIA_ALLOCMEM / VIA_FREEMEM.
 */

int via_agp_init_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	drm_via_agp_t *agp = data;
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	mutex_lock(&dev->struct_mutex);
	drm_mm_init(&dev_priv->agp_mm, 0, agp->size >> VIA_MM_ALIGN_SHIFT);

	dev_priv->agp_initialized = 1;
	dev_priv->agp_offset = agp->offset;
	mutex_unlock(&dev->struct_mutex);

	DRM_DEBUG("offset = %u, size = %u\n", agp->offset, agp->size);
	return 0;
}

int via_fb_init_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_priv)
{
	drm_via_fb_t *fb = data;
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	mutex_lock(&dev->struct_mutex);
	drm_mm_init(&dev_priv->vram_mm, 0, fb->size >> VIA_MM_ALIGN_SHIFT);

	dev_priv->vram_initialized = 1;
	dev_priv->vram_offset = fb->offset;

	mutex_unlock(&dev->struct_mutex);
	DRM_DEBUG("offset = %u, size = %u\n", fb->offset, fb->size);

	return 0;

}

int via_mem_alloc_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file)
{
	drm_via_mem_t *mem = data;
	int retval = 0, user_key;
	struct via_memblock *item;
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	struct via_file_private *file_priv = file->driver_priv;
	unsigned long tmpSize;

	if (mem->type > VIA_MEM_AGP) {
		DRM_ERROR("Unknown memory type allocation\n");
		return -EINVAL;
	}
	mutex_lock(&dev->struct_mutex);
	if (0 == ((mem->type == VIA_MEM_VIDEO) ? dev_priv->vram_initialized :
		      dev_priv->agp_initialized)) {
		mutex_unlock(&dev->struct_mutex);
		DRM_ERROR
		    ("Attempt to allocate from uninitialized memory manager.\n");
		return -EINVAL;
	}

	item = kzalloc(sizeof(*item), GFP_KERNEL);
	if (!item) {
		retval = -ENOMEM;
		goto fail_alloc;
	}

	tmpSize = (mem->size + VIA_MM_ALIGN_MASK) >> VIA_MM_ALIGN_SHIFT;
	if (mem->type == VIA_MEM_AGP)
		retval = drm_mm_insert_node(&dev_priv->agp_mm,
					    &item->mm_node,
					    tmpSize);
	else
		retval = drm_mm_insert_node(&dev_priv->vram_mm,
					    &item->mm_node,
					    tmpSize);
	if (retval)
		goto fail_alloc;

	retval = idr_alloc(&dev_priv->object_idr, item, 1, 0, GFP_KERNEL);
	if (retval < 0)
		goto fail_idr;
	user_key = retval;
	item->idr_key = user_key;

	list_add(&item->owner_list, &file_priv->obj_list);
	mutex_unlock(&dev->struct_mutex);

	mem->offset = ((mem->type == VIA_MEM_VIDEO) ?
		      dev_priv->vram_offset : dev_priv->agp_offset) +
	    ((item->mm_node.start) << VIA_MM_ALIGN_SHIFT);
	mem->index = user_key;

	return 0;

fail_idr:
	drm_mm_remove_node(&item->mm_node);
fail_alloc:
	kfree(item);
	mutex_unlock(&dev->struct_mutex);

	mem->offset = 0;
	mem->size = 0;
	mem->index = 0;
	DRM_DEBUG("Video memory allocation failed\n");

	return retval;
}

int via_mem_free_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	drm_via_mem_t *mem = data;
	struct via_memblock *obj;

	mutex_lock(&dev->struct_mutex);
	obj = idr_find(&dev_priv->object_idr, mem->index);
	if (obj == NULL) {
		mutex_unlock(&dev->struct_mutex);
		return -EINVAL;
	}

	idr_remove(&dev_priv->object_idr, mem->index);
	list_del(&obj->owner_list);
	drm_mm_remove_node(&obj->mm_node);
	kfree(obj);
	mutex_unlock(&dev->struct_mutex);

	DRM_DEBUG("free = 0x%lx\n", mem->index);

	return 0;
}

/*
 * Free all memory blocks owned by a closing client. Called from the
 * driver's postclose hook, since the DRM core only invokes the legacy
 * preclose callback for DRIVER_LEGACY drivers.
 */
void via_driver_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	struct via_file_private *file_priv = file->driver_priv;
	struct via_memblock *entry, *next;

	if (!file_priv)
		return;

	mutex_lock(&dev_priv->ring_lock);
	via_driver_dma_quiescent(dev);
	mutex_unlock(&dev_priv->ring_lock);

	mutex_lock(&dev->struct_mutex);
	if (!list_empty(&file_priv->obj_list)) {
		list_for_each_entry_safe(entry, next, &file_priv->obj_list,
					 owner_list) {
			list_del(&entry->owner_list);
			idr_remove(&dev_priv->object_idr, entry->idr_key);
			drm_mm_remove_node(&entry->mm_node);
			kfree(entry);
		}
	}
	mutex_unlock(&dev->struct_mutex);

	kfree(file_priv);
	file->driver_priv = NULL;
}

void via_ring_lastclose(struct drm_device *dev)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	if (!dev_priv)
		return;

	mutex_lock(&dev->struct_mutex);
	if (dev_priv->vram_initialized) {
		drm_mm_takedown(&dev_priv->vram_mm);
		dev_priv->vram_initialized = 0;
	}
	if (dev_priv->agp_initialized) {
		drm_mm_takedown(&dev_priv->agp_mm);
		dev_priv->agp_initialized = 0;
	}
	mutex_unlock(&dev->struct_mutex);
}

static int via_chipset_from_pci(u16 device)
{
	switch (device) {
	case 0x3022:	/* CLE266 */
	case 0x3122:	/* KM400 / K8M800 */
	case 0x7205:	/* CN400 */
	case 0x3108:	/* P4M800 */
	case 0x3344:	/* PM800 */
	case 0x3343:	/* PN800 */
	case 0x3371:	/* VX800 / VX855 / VX900 */
	case 0x1122:	/* CX700 */
	case 0x5122:	/* VX855 */
	case 0x7122:	/* CX700M */
		return VIA_OTHER;
	case 0x3118:	/* KM400 */
	case 0x3157:	/* K8M800 / K8N800 */
		return VIA_PRO_GROUP_A;
	case 0x3230:	/* K8M890 */
		return VIA_DX9_0;
	default:
		return VIA_OTHER;
	}
}

/*
 * Set up the legacy ring / AGP / vblank / IRQ infrastructure. Called from
 * via_drm_init after the device (including MMIO) has been initialized.
 */
int via_ring_legacy_init(struct drm_device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	int ret;

	mutex_init(&dev_priv->ring_lock);
	idr_init_base(&dev_priv->object_idr, 1);

	dev_priv->chipset = via_chipset_from_pci(pdev->device);

	via_init_command_verifier();

	ret = drm_vblank_init(dev, VIA_MAX_CRTC);
	if (ret)
		return ret;

	/* The AGP aperture setup now happens in via_device_init() so that
	 * gtt_size is available when via_mm_init() sizes the TTM GTT
	 * range manager. dev->agp is therefore already valid here. */

	pci_set_master(pdev);

	/* Install the IRQ handler ourselves.
	 * Initialize the per-irq bookkeeping unconditionally,
	 * but only install a handler and arm the hardware
	 * (postinstall) when the kernel actually assigned us a
	 * usable IRQ line (pdev->irq).
	 *
	 * We deliberately do NOT fall back to reading the stale
	 * PCI_INTERRUPT_LINE config register: on some boards
	 * (e.g. the Fujitsu-Siemens Amilo Pro V2035d, where the
	 * GPU's INT is routed to the misconfigured level-triggered
	 * timer IRQ 0/GSI-16) that line is not a working interrupt
	 * source, and arming the GPU's VBLANK interrupt there would
	 * assert a level interrupt that has no EOI capability, causing
	 * a NULL-deref panic.
	 */
	via_ring_irq_preinstall(dev);

	dev->irq = pdev->irq;
	drm_info(dev, "irq=%u (pdev->irq=%u)\n", dev->irq, pdev->irq);
	if (dev->irq) {
		ret = devm_request_irq(&pdev->dev, dev->irq,
				       via_ring_irq_handler,
				       IRQF_SHARED, DRIVER_NAME, dev);
		if (ret) {
			drm_warn(dev, "Failed to install IRQ handler: %d\n",
				 ret);
			dev->irq = 0;
		}
	} else {
		drm_warn(dev, "No PCI IRQ assigned; ring (DRI1) "
				 "will not be interrupt-driven\n");
	}

	if (dev->irq)
		via_ring_irq_postinstall(dev);

	return 0;
}

void via_ring_legacy_fini(struct drm_device *dev)
{
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	via_ring_irq_uninstall(dev);

	via_dma_cleanup(dev);

	idr_destroy(&dev_priv->object_idr);
	mutex_destroy(&dev_priv->ring_lock);
}
