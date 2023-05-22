// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2012 James Simmons. All Rights Reserved.
 * Copyright 1998-2009 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.
 *
 * Author(s):
 * James Simmons <jsimmons@infradead.org>
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>

#include <uapi/linux/i2c.h>

#include "via_drv.h"

#define SERIAL	0
#define	GPIO	1

struct via_i2c_stuff {
	u16 i2c_port;			/* GPIO or I2C port */
	u16 is_active;			/* Being used as I2C? */
	struct i2c_adapter adapter;
	struct i2c_algo_bit_data algo;
};

static struct via_i2c_stuff via_i2c_par[5];

static void via_i2c_setsda(void *data, int state)
{
	struct via_i2c_stuff *i2c = data;
	struct drm_device *dev = i2c_get_adapdata(&i2c->adapter);
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	u8 value, mask;

	if (i2c->is_active == GPIO) {
		mask = state ? BIT(6) : BIT(6) | BIT(4);
		value = state ? 0x00 : BIT(6);
	} else {
		value = state ? BIT(4) | BIT(0) : BIT(0);
		mask = BIT(4) | BIT(0);
	}

	svga_wseq_mask(VGABASE, i2c->i2c_port, value, mask);
}

static void via_i2c_setscl(void *data, int state)
{
	struct via_i2c_stuff *i2c = data;
	struct drm_device *dev = i2c_get_adapdata(&i2c->adapter);
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);
	u8 value, mask;

	if (i2c->is_active == GPIO) {
		mask = state ? BIT(7) : BIT(7) | BIT(5);
		value = state ? 0x00 : BIT(7);
	} else {
		value = state ? BIT(5) | BIT(0) : BIT(0);
		mask = BIT(5) | BIT(0);
	}

	svga_wseq_mask(VGABASE, i2c->i2c_port, value, mask);
}

static int via_i2c_getsda(void *data)
{
	struct via_i2c_stuff *i2c = data;
	struct drm_device *dev = i2c_get_adapdata(&i2c->adapter);
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	return vga_rseq(VGABASE, i2c->i2c_port) & BIT(2);
}

static int via_i2c_getscl(void *data)
{
	struct via_i2c_stuff *i2c = data;
	struct drm_device *dev = i2c_get_adapdata(&i2c->adapter);
	struct via_drm_priv *dev_priv = to_via_drm_priv(dev);

	return vga_rseq(VGABASE, i2c->i2c_port) & BIT(3);
}

struct i2c_adapter *via_find_ddc_bus(int port)
{
	struct i2c_adapter *adapter = NULL;
	int i;

	for (i = 0; i < ARRAY_SIZE(via_i2c_par); i++) {
		struct via_i2c_stuff *i2c = &via_i2c_par[i];

		if (i2c->i2c_port == port) {
			adapter = &i2c->adapter;
			break;
		}
	}
	return adapter;
}

static int create_i2c_bus(struct drm_device *dev,
				struct via_i2c_stuff *i2c_par)
{
	struct i2c_adapter *adapter = &i2c_par->adapter;
	struct i2c_algo_bit_data *algo = &i2c_par->algo;

	algo->setsda = via_i2c_setsda;
	algo->setscl = via_i2c_setscl;
	algo->getsda = via_i2c_getsda;
	algo->getscl = via_i2c_getscl;
	algo->udelay = 15;
	algo->timeout = usecs_to_jiffies(2200); /* from VESA */
	algo->data = i2c_par;

	sprintf(adapter->name, "via i2c bit bus 0x%02x", i2c_par->i2c_port);
	adapter->owner = THIS_MODULE;
	adapter->class = I2C_CLASS_DDC;
	adapter->algo_data = algo;
	i2c_set_adapdata(adapter, dev);

	/* Raise SCL and SDA */
	via_i2c_setsda(i2c_par, 1);
	via_i2c_setscl(i2c_par, 1);
	udelay(20);

	return i2c_bit_add_bus(adapter);
}

void via_i2c_readbytes(struct i2c_adapter *adapter,
			u8 slave_addr, char offset,
			u8 *buffer, unsigned int size)
{
	u8 out_buf[2];
	u8 in_buf[2];
	struct i2c_msg msgs[] = {
		{
			.addr = slave_addr,
			.flags = 0,
			.len = 1,
			.buf = out_buf,
		},
		{
			.addr = slave_addr,
			.flags = I2C_M_RD,
			.len = size,
			.buf = in_buf,
		}
	};

	out_buf[0] = offset;
	out_buf[1] = 0;

	if (i2c_transfer(adapter, msgs, 2) != 2)
		printk(KERN_WARNING "%s failed\n", __func__);
	else
		*buffer = in_buf[0];
}

void via_i2c_writebytes(struct i2c_adapter *adapter,
			u8 slave_addr, char offset,
			u8 *data, unsigned int size)
{
	struct i2c_msg msg = { 0 };
	u8 *out_buf;

	out_buf = kzalloc(size + 1, GFP_KERNEL);
	out_buf[0] = offset;
	memcpy(&out_buf[1], data, size);
	msg.addr = slave_addr;
	msg.flags = 0;
	msg.len = size + 1;
	msg.buf = out_buf;

	if (i2c_transfer(adapter, &msg, 1) != 1)
		printk(KERN_WARNING "%s failed\n", __func__);

	kfree(out_buf);
}

void via_i2c_reg_init(struct via_drm_priv *dev_priv)
{
	svga_wseq_mask(VGABASE, 0x31, 0x30, 0x30);
	svga_wseq_mask(VGABASE, 0x26, 0x30, 0x30);
	vga_wseq(VGABASE, 0x2C, 0xc2);
	vga_wseq(VGABASE, 0x3D, 0xc0);
	svga_wseq_mask(VGABASE, 0x2C, 0x30, 0x30);
	svga_wseq_mask(VGABASE, 0x3D, 0x30, 0x30);
}

int via_i2c_init(struct drm_device *dev)
{
	int types[] = { SERIAL, SERIAL, GPIO, GPIO, GPIO };
	int ports[] = { 0x26, 0x31, 0x25, 0x2C, 0x3D };
	int count = ARRAY_SIZE(via_i2c_par), ret, i;
	struct via_i2c_stuff *i2c = via_i2c_par;

	for (i = 0; i < count; i++) {
		i2c->is_active = types[i];
		i2c->i2c_port = ports[i];

		ret = create_i2c_bus(dev, i2c);
		if (ret < 0)
			drm_err(dev, "cannot create i2c bus %x:%d\n",
					ports[i], ret);
		i2c++;
	}
	return 0;
}

void via_i2c_exit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(via_i2c_par); i++)
		i2c_del_adapter(&via_i2c_par->adapter);
}
