/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * HD audio interface patch for Realtek ALC codecs
 *
 * Copyright (c) 2004 Kailang Yang <kailang@realtek.com.tw>
 *                    PeiSen Hou <pshou@realtek.com.tw>
 *                    Takashi Iwai <tiwai@suse.de>
 *                    Jonathan Woithe <jwoithe@just42.net>
 *
 *  This driver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This driver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * Taken from linux's patch_realtek.c
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>

#include <sys/ctype.h>

#include <dev/sound/pci/hda/hdac.h>
#include <dev/sound/pci/hda/hdaa.h>
#include <dev/sound/pci/hda/hda_reg.h>
#include <dev/sound/pci/hda/hdaa_patches.h>

void
hdaa_patch_direct_acer_c720(struct hdaa_devinfo *devinfo)
{
	struct hdaa_widget *w;
	device_t dev = devinfo->dev;
	uint32_t val;

	kprintf("Acer C720 patch\n");

	/* power down control */
	hda_write_coef_idx(dev, 0x20, 0x03, 0x0002);
	/* FIFO and filter clock */
	hda_write_coef_idx(dev, 0x20, 0x05, 0x0700);
	/* DMIC control */
	hda_write_coef_idx(dev, 0x20, 0x07, 0x0200);
	/* Analog clock */
	val = hda_read_coef_idx(dev, 0x20, 0x06);
	hda_write_coef_idx(dev, 0x20, 0x06, (val & ~0x00f0) | 0x0);

	/* JD */
	val = hda_read_coef_idx(dev, 0x20, 0x08);
	hda_write_coef_idx(dev, 0x20, 0x08, (val & ~0xfffc) | 0x0c2c);
	/* JD offset1 */
	hda_write_coef_idx(dev, 0x20, 0x0a, 0xcccc);
	/* JD offset2 */
	hda_write_coef_idx(dev, 0x20, 0x0b, 0xcccc);
	/* LD0/1/2/3, DAC/ADC */
	hda_write_coef_idx(dev, 0x20, 0x0e, 0x6fc0);
	/* JD */
	val = hda_read_coef_idx(dev, 0x20, 0x0f);
	hda_write_coef_idx(dev, 0x20, 0x0f, (val & ~0xf800) | 0x1000);

	/* Capless */
	val = hda_read_coef_idx(dev, 0x20, 0x10);
	hda_write_coef_idx(dev, 0x20, 0x10, (val & ~0xfc00) | 0x0c00);
	/* Class D test 4 */
	hda_write_coef_idx(dev, 0x20, 0x3a, 0x0);
	/* IO power down directly */
	val = hda_read_coef_idx(dev, 0x20, 0x0c);
	hda_write_coef_idx(dev, 0x20, 0x0c, (val & ~0xfe00) | 0x0);
	/* ANC */
	hda_write_coef_idx(dev, 0x20, 0x22, 0xa0c0);
	/* AGC MUX */
	val = hda_read_coef_idx(dev, 0x53, 0x01);
	hda_write_coef_idx(dev, 0x53, 0x01, (val & ~0x000f) | 0x0008);

	/* DAC simple content protection */
	val = hda_read_coef_idx(dev, 0x20, 0x1d);
	hda_write_coef_idx(dev, 0x20, 0x1d, (val & ~0x00e0) | 0x0);
	/* ADC simple content protection */
	val = hda_read_coef_idx(dev, 0x20, 0x1f);
	hda_write_coef_idx(dev, 0x20, 0x1f, (val & ~0x00e0) | 0x0);
	/* DAC ADC Zero Detection */
	hda_write_coef_idx(dev, 0x20, 0x21, 0x8804);
	/* PLL */
	hda_write_coef_idx(dev, 0x20, 0x2e, 0x2902);
	/* capless control 2 */
	hda_write_coef_idx(dev, 0x20, 0x33, 0xa080);
	/* capless control 3 */
	hda_write_coef_idx(dev, 0x20, 0x34, 0x3400);
	/* capless control 4 */
	hda_write_coef_idx(dev, 0x20, 0x35, 0x2f3e);
	/* capless control 5 */
	hda_write_coef_idx(dev, 0x20, 0x36, 0x0);
	/* class D test 2 */
	val = hda_read_coef_idx(dev, 0x20, 0x38);
	hda_write_coef_idx(dev, 0x20, 0x38, (val & ~0x0fff) | 0x0900);

	/* class D test 3 */
	hda_write_coef_idx(dev, 0x20, 0x39, 0x110a);
	/* class D test 5 */
	val = hda_read_coef_idx(dev, 0x20, 0x3b);
	hda_write_coef_idx(dev, 0x20, 0x3b, (val & ~0x00f8) | 0x00d8);
	/* class D test 6 */
	hda_write_coef_idx(dev, 0x20, 0x3c, 0x0014);
	/* classD OCP */
	hda_write_coef_idx(dev, 0x20, 0x3d, 0xc2ba);
	/* classD pure DC test */
	val = hda_read_coef_idx(dev, 0x20, 0x42);
	hda_write_coef_idx(dev, 0x20, 0x42, (val & ~0x0f80) | 0x0);
	/* test mode */
	hda_write_coef_idx(dev, 0x20, 0x49, 0x0);
	/* Class D DC enable */
	val = hda_read_coef_idx(dev, 0x20, 0x40);
	hda_write_coef_idx(dev, 0x20, 0x40, (val & ~0xf800) | 0x9800);
	/* DC offset */
	val = hda_read_coef_idx(dev, 0x20, 0x42);
	hda_write_coef_idx(dev, 0x20, 0x42, (val & ~0xf000) | 0x2000);
	/* Class D amp control */
	hda_write_coef_idx(dev, 0x20, 0x37, 0xfc06);

	/* Index 0x43 direct drive HP AMP LPM Control 1 */
	/* Headphone capless set to high power mode */
	hda_write_coef_idx(dev, 0x20, 0x43, 0x9004);

#if 0
	/*
	 * This has to do with the 'mute internal speaker when
	 * ext headphone out jack is plugged' function.  nid 27
	 * comes from the special config bits (XXX currently hardwired)
	 *
	 * XXX doesn't apply to chromebook where we just have to change
	 *	the mixer selection for nid 33.
	 */
	int dummy;
	hda_command(dev, HDA_CMD_SET_AMP_GAIN_MUTE(0, 27, 0xb080));
	tsleep(&dummy, 0, "hdaslp", hz / 10);
	hda_command(dev, HDA_CMD_SET_PIN_WIDGET_CTRL(0, 27,
				HDA_CMD_SET_PIN_WIDGET_CTRL_OUT_ENABLE));
	tsleep(&dummy, 0, "hdaslp", hz / 10);
#endif

	/* 0x46 combo jack auto switch control 2 */
	/* 3k pull-down control for headset jack. */
	val = hda_read_coef_idx(dev, 0x20, 0x46);
	hda_write_coef_idx(dev, 0x20, 0x46, val & ~(3 << 12));
	/* headphone capless set to normal mode */
	hda_write_coef_idx(dev, 0x20, 0x43, 0x9614);

#if 0
	/*
	 * Fixup chromebook (? which chromebook?)
	 */
	/* MIC2-VREF control */
	/* set to manual mode */
	val = hda_read_coef_idx(dev, 0x20, 0x06);
	hda_write_coef_idx(dev, 0x20, 0x06, val & ~0x000c);
	/* enable line1 input control by verb */
	val = hda_read_coef_idx(dev, 0x20, 0x1a);
	hda_write_coef_idx(dev, 0x20, 0x1a, val | (1 << 4));
#endif

	/*
	 * 31-30	: port connectivity
	 * 29-21	: reserved
	 * 20		: PCBEEP input
	 * 19-16	: checksum (15:1)
	 * 15-1		: Custom
	 * 0		: Override
	 *
	 * XXX this needs code from linux patch_realtek.c alc_subsystem_id().
	 * Chromebook: 0x4015812d
	 *	bit 30	physical connection present
	 *	bit 15	if set we want the 'mute internal speaker when
	 *		ext headphone out jack is plugged' function
	 *	    bit 14:13	reserved
	 *	    bit 12:11	headphone out 00: PortA, 01: PortE, 02: PortD,
	 *				      03: Reserved (for C720 this is 0)
	 *	    bit 10:8    jack location (for c720 this is 1)
	 *				0, 0x1b, 0x14, 0x21 - nnid 27 is jack?
	 *
	 *	bit 5:3	-> 101 (5).  ALC_INIT_DEFAULT (default ext amp ctl)
	 *	bit 0	override (is set)
	 */
	if ((w = hdaa_widget_get(devinfo, 0x1d)) != NULL) {
		kprintf("WIDGET SPECIAL: %08x\n", w->wclass.pin.config);
		/* XXX currently in hdaa_patch_direct_acer_c720(devinfo); */
	}
}
