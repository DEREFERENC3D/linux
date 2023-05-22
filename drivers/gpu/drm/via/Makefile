#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

ccflags-y := -Iinclude/drm
via-y := via_connector.o \
		via_crtc.o \
		via_crtc_hw.o \
		via_cursor.o \
		via_dac.o \
		via_drv.o \
		via_encoder.o \
		via_hdmi.o \
		via_i2c.o \
		via_init.o \
		via_ioctl.o \
		via_lvds.o \
		via_object.o \
		via_pll.o \
		via_pm.o \
		via_sii164.o \
		via_tmds.o \
		via_ttm.o \
		via_tx.o \
		via_vt1632.o

obj-$(CONFIG_DRM_VIA)	+= via.o
