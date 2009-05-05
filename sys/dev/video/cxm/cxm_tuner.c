/*
 * Copyright (c) 2003, 2004, 2005
 *	John Wehle <john@feith.com>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by John Wehle.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.	IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Tuner routines for the Conexant MPEG-2 Codec driver.
 *
 * Ideally these routines should be implemented as a separate
 * driver which has a generic tuner interface so that it's
 * not necessary for each multimedia driver to re-invent the
 * wheel.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/kernel.h>
#include <sys/poll.h>
#include <sys/select.h>
#include <sys/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <machine/clock.h>

#include <dev/video/meteor/ioctl_meteor.h>
#include <dev/video/bktr/ioctl_bt848.h>

#include <dev/video/cxm/cxm.h>

#include <bus/iicbus/iiconf.h>
#include <bus/iicbus/iicbus.h>

#include "iicbb_if.h"


/*
 * Channel mappings derived from
 * http://developer.apple.com/technotes/tn/tn1012.html
 *
 * B/G Cable mapping based the bktr Western European mapping
 */

static const struct cxm_tuner_channels
us_air_channels = {
	"US Broadcast",
	CHNLSET_NABCST,
	CXM_TUNER_TV_SYSTEM_MN,
	2,
	69,
	45750,
	{ { 14, 471250, 6000 },
	  { 7, 175250, 6000 },
	  { 5, 77250, 6000 },
	  { 2, 55250, 6000 } }
};

static const struct cxm_tuner_channels
us_cable_channels = {
	"US Cable",
	CHNLSET_CABLEIRC,
	CXM_TUNER_TV_SYSTEM_MN,
	1,
	125,
	45750,
	{ { 100, 649250, 6000 },
	  { 95, 91250, 6000 },
	  { 23, 217250, 6000 },
	  { 14, 121250, 6000 },
	  { 7, 175250, 6000 },
	  { 5, 77250, 6000 },
	  { 2, 55250, 6000 },
	  { 1, 73250, 6000 } }
};

static const struct cxm_tuner_channels
bg_air_channels = {
	"B/G Broadcast",
	0,
	CXM_TUNER_TV_SYSTEM_BG,
	2,
	89,
	38900,
	{ { 82, 175250, 7000 },
	  { 80, 55250, 7000 },
	  { 79, 217250, 7000 },
	  { 77, 209250, 7000 },
	  { 76, 138250, 9000 },
	  { 75, 102250, 9000 },
	  { 73, 86250, 9000 },
	  { 72, 64250, 7500 },
	  { 70, 49750, 7500 },
	  { 21, 471250, 8000 },
	  { 20, 210250, 8500 },
	  { 18, 192750, 8500 },
	  { 16, 175250, 8000 },
	  { 15, 82250, 8500 },
	  { 13, 53750, 8500 },
	  { 5, 175250, 7000 },
	  { 2, 48250, 7000 } }
};

static const struct cxm_tuner_channels
bg_cable_channels = {
	"B/G Cable",
	CHNLSET_WEUROPE,
	CXM_TUNER_TV_SYSTEM_BG,
	2,
	120,
	38900,
	{ { 100, 303250, 8000 },
	  { 90, 231250, 7000 },
	  { 80, 105250, 7000 },
	  { 79, 0, 0 },
	  { 78, 0, 0 },
	  { 77, 0, 0 },
	  { 74, 69250, 7000 },
	  { 73, 0, 0 },
	  { 70, 45750, 8000 },
	  { 21, 471250, 8000 },
	  { 20, 210250, 8500 },
	  { 18, 192750, 8500 },
	  { 16, 175250, 8000 },
	  { 15, 82250, 8500 },
	  { 13, 53750, 8500 },
	  { 5, 175250, 7000 },
	  { 2, 48250, 7000 } }
};

static const struct cxm_tuner_channels
bg_australia_channels = {
	"B/G Australia",
	CHNLSET_AUSTRALIA,
	CXM_TUNER_TV_SYSTEM_BG,
	1,
	83,
	38900,
	{ { 28, 527250, 7000 },
	  { 10, 209250, 7000 },
	  { 6, 175250, 7000 },
	  { 4, 95250, 7000 },
	  { 3, 86250, 7000 },
	  { 1, 57250, 7000 } }
};

static const struct cxm_tuner_channels
i_air_channels = {
	"I Broadcast",
	0,
	CXM_TUNER_TV_SYSTEM_I,
	1,
	83,
	38900,
	{ { 75, 179750, 5000 },
	  { 71, 51750, 5000 },
	  { 70, 45000, 5000 },
	  { 21, 471250, 8000 },
	  { 20, 0, 0 },
	  { 19, 0, 0 },
	  { 18, 0, 0 },
	  { 17, 0, 0 },
	  { 16, 0, 0 },
	  { 15, 0, 0 },
	  { 14, 0, 0 },
	  { 13, 247250, 8000 },
	  { 12, 0, 0 },
	  { 4, 175250, 8000 },
	  { 1, 45750, 8000 } }
};

static const struct cxm_tuner_channels
l_air_channels = {
	"L Broadcast",
	CHNLSET_FRANCE,
	CXM_TUNER_TV_SYSTEM_L,
	1,
	69,
	38900,
	{ { 21, 471250, 8000 },
	  { 20, 0, 0 },
	  { 19, 0, 0 },
	  { 18, 0, 0 },
	  { 17, 0, 0 },
	  { 16, 0, 0 },
	  { 15, 0, 0 },
	  { 14, 0, 0 },
	  { 8, 176000, 8000 },
	  { 5, 57250, 3250 },
	  { 4, 55750, 1500 },
	  { 3, 54000, 1750 },
	  { 2, 49250, 4750 },
	  { 1, 47750, 1500 } }
};

static const struct cxm_tuner_channels
japan_air_channels = {
	"Japan Broadcast",
	CHNLSET_JPNBCST,
	CXM_TUNER_TV_SYSTEM_MN,
	1,
	62,
	45750,
	{ { 13, 471250, 6000 },
	  { 8, 193250, 6000 },
	  { 4, 171250, 6000 },
	  { 1, 91250, 6000 } }
};

static const struct cxm_tuner_channels
japan_cable_channels = {
	"Japan Cable",
	CHNLSET_JPNCABLE,
	CXM_TUNER_TV_SYSTEM_MN,
	1,
	63,
	45750,
	{ { 23, 223250, 6000 },
	  { 22, 165250, 6000 },
	  { 13, 109250, 6000 },
	  { 8, 193250, 6000 },
	  { 4, 171250, 6000 },
	  { 1, 91250, 6000 } }
};


static const struct cxm_tuner_channels
*channel_sets[] = {
	&us_air_channels,
	&us_cable_channels,
	&bg_air_channels,
	&bg_cable_channels,
	&bg_australia_channels,
	&i_air_channels,
	&l_air_channels,
	&japan_air_channels,
	&japan_cable_channels
};


const struct cxm_tuner
cxm_tuners[CXM_TUNER_TYPES] = {
	{ "Philips FI1216 MK2",
		{ CXM_TUNER_TV_SYSTEM_BG, cxm_none_system_code_style },
		48250, 855250,
		{ { 450000, { 0x8e, 0x30 } },
		  { 170000, { 0x8e, 0x90 } },
		  { 48250, { 0x8e, 0xa0 } } },
		0, 0,
		{ 0 },
		&bg_air_channels },
	{ "Philips FM1216",
		{ CXM_TUNER_TV_SYSTEM_BG, cxm_none_system_code_style },
		48250, 855250,
		{ { 450000, { 0xce, 0x30 } },
		  { 170000, { 0xce, 0x90 } },
		  { 48250, { 0xce, 0xa0 } } },
		87500, 108000,
		{ 87500, { 0x88, 0xa5 } },
		&bg_air_channels },
	{ "Philips FQ1216ME",
		{ CXM_TUNER_TV_SYSTEM_BG | CXM_TUNER_TV_SYSTEM_DK
		  | CXM_TUNER_TV_SYSTEM_I
		  | CXM_TUNER_TV_SYSTEM_L | CXM_TUNER_TV_SYSTEM_L_PRIME,
		  cxm_port_system_code_style,
		  { { CXM_TUNER_TV_SYSTEM_BG,      { 0x09 } },
		    { CXM_TUNER_TV_SYSTEM_DK,      { 0x09 } },
		    { CXM_TUNER_TV_SYSTEM_I,       { 0x01 } },
		    { CXM_TUNER_TV_SYSTEM_L,       { 0x0b } },
		    { CXM_TUNER_TV_SYSTEM_L_PRIME, { 0x0a } } } },
		48250, 855250,
		{ { 450000, { 0x8e, 0x30 } },
		  { 170000, { 0x8e, 0x90 } },
		  { 48250, { 0x8e, 0xa0 } } },
		0, 0,
		{ 0 },
		&l_air_channels },
	{ "Philips FQ1216ME MK3",
		{ CXM_TUNER_TV_SYSTEM_BG | CXM_TUNER_TV_SYSTEM_DK
		  | CXM_TUNER_TV_SYSTEM_I
		  | CXM_TUNER_TV_SYSTEM_L | CXM_TUNER_TV_SYSTEM_L_PRIME,
		  cxm_if_system_with_aux_code_style,
		  { { CXM_TUNER_TV_SYSTEM_BG,      { 0x16, 0x70, 0x49, 0x40 }},
		    { CXM_TUNER_TV_SYSTEM_DK,      { 0x16, 0x70, 0x4b, 0x40 }},
		    { CXM_TUNER_TV_SYSTEM_I,       { 0x16, 0x70, 0x4a, 0x40 }},
		    { CXM_TUNER_TV_SYSTEM_L,       { 0x06, 0x50, 0x4b, 0x50 }},
		    { CXM_TUNER_TV_SYSTEM_L_PRIME, { 0x86, 0x50, 0x53, 0x50 }}
		    } },
		48250, 863250,
		{ { 442000, { 0xce, 0x04 } },
		  { 160000, { 0xce, 0x02 } },
		  { 48250, { 0xce, 0x01 } } },
		0, 0,
		{ 0 },
		&l_air_channels },
	{ "Philips FM1216ME MK3",
		{ CXM_TUNER_TV_SYSTEM_BG | CXM_TUNER_TV_SYSTEM_DK
		  | CXM_TUNER_TV_SYSTEM_I
		  | CXM_TUNER_TV_SYSTEM_L | CXM_TUNER_TV_SYSTEM_L_PRIME,
		  cxm_if_system_with_aux_code_style,
		  { { CXM_TUNER_TV_SYSTEM_BG,      { 0x16, 0x70, 0x49, 0x40 }},
		    { CXM_TUNER_TV_SYSTEM_DK,      { 0x16, 0x70, 0x4b, 0x40 }},
		    { CXM_TUNER_TV_SYSTEM_I,       { 0x16, 0x70, 0x4a, 0x40 }},
		    { CXM_TUNER_TV_SYSTEM_L,       { 0x06, 0x50, 0x4b, 0x50 }},
		    { CXM_TUNER_TV_SYSTEM_L_PRIME, { 0x86, 0x50, 0x53, 0x50 }},
		    { CXM_TUNER_FM_SYSTEM,         { 0x0a, 0x90, 0x20, 0x40 }}
		    } },
		48250, 863250,
		{ { 442000, { 0xce, 0x04 } },
		  { 160000, { 0xce, 0x02 } },
		  { 48250, { 0xce, 0x01 } } },
		87500, 108000,
		{ 87500, { 0x88, 0x19 } },
		&l_air_channels },
	{ "Philips FI1236 MK2",
		{ CXM_TUNER_TV_SYSTEM_MN, cxm_none_system_code_style },
		55250, 801250,
		{ { 454000, { 0x8e, 0x30 } },
		  { 160000, { 0x8e, 0x90 } },
		  { 55250, { 0x8e, 0xa0 } } },
		0, 0,
		{ 0 },
		&us_cable_channels },
	{ "Philips FM1236",
		{ CXM_TUNER_TV_SYSTEM_MN, cxm_none_system_code_style },
		55250, 801250,
		{ { 454000, { 0xce, 0x30 } },
		  { 160000, { 0xce, 0x90 } },
		  { 55250, { 0xce, 0xa0 } } },
		76000, 108000,
		{ 76000, { 0x88, 0xa5 } },
		&us_cable_channels },
	{ "Philips FI1246 MK2",
		{ CXM_TUNER_TV_SYSTEM_I, cxm_none_system_code_style },
		45750, 855250,
		{ { 450000, { 0x8e, 0x30 } },
		  { 170000, { 0x8e, 0x90 } },
		  { 45750, { 0x8e, 0xa0 } } },
		0, 0,
		{ 0 },
		&i_air_channels },
	{ "Philips FM1246",
		{ CXM_TUNER_TV_SYSTEM_I, cxm_none_system_code_style },
		45750, 855250,
		{ { 450000, { 0xce, 0x30 } },
		  { 170000, { 0xce, 0x90 } },
		  { 45750, { 0xce, 0xa0 } } },
		87500, 108000,
		{ 87500, { 0x88, 0xa5 } },
		&i_air_channels },
	{ "Temic 4006 FH5",
		{ CXM_TUNER_TV_SYSTEM_BG, cxm_none_system_code_style },
		48250, 855250,
		{ { 454000, { 0x8e, 0x30 } },
		  { 169000, { 0x8e, 0x90 } },
		  { 48250, { 0x8e, 0xa0 } } },
		0, 0,
		{ 0 },
		&bg_air_channels },
	{ "Temic 4009 FR5",
		{ CXM_TUNER_TV_SYSTEM_BG, cxm_none_system_code_style },
		48250, 855250,
		{ { 464000, { 0x8e, 0x30 } },
		  { 141000, { 0x8e, 0x90 } },
		  { 48250, { 0x8e, 0xa0 } } },
		87500, 108100,
		{ 87500, { 0x88, 0xa5 } },
		&bg_air_channels },
	{ "Temic 4036 FY5",
		{ CXM_TUNER_TV_SYSTEM_MN, cxm_none_system_code_style },
		55250, 801250,
		{ { 453000, { 0x8e, 0x30 } },
		  { 158000, { 0x8e, 0x90 } },
		  { 55250, { 0x8e, 0xa0 } } },
		0, 0,
		{ 0 },
		&us_cable_channels },
	{ "Temic 4039 FR5",
		{ CXM_TUNER_TV_SYSTEM_MN, cxm_none_system_code_style },
		55250, 801250,
		{ { 453000, { 0x8e, 0x30 } },
		  { 158000, { 0x8e, 0x90 } },
		  { 55250, { 0x8e, 0xa0 } } },
		75900, 108100,
		{ 75900, { 0x88, 0xa5 } },
		&us_cable_channels },
	{ "Temic 4066 FY5",
		{ CXM_TUNER_TV_SYSTEM_I, cxm_none_system_code_style },
		45750, 855250,
		{ { 454000, { 0x8e, 0x30 } },
		  { 169000, { 0x8e, 0x90 } },
		  { 45750, { 0x8e, 0xa0 } } },
		0, 0,
		{ 0 },
		&i_air_channels },
	{ "LG Innotek TPI8PSB11D",
		{ CXM_TUNER_TV_SYSTEM_BG, cxm_none_system_code_style },
		48250, 855250,
		{ { 450000, { 0x8e, 0x30 } },
		  { 170000, { 0x8e, 0x90 } },
		  { 48250, { 0x8e, 0xa0 } } },
		0, 0,
		{ 0 },
		&bg_air_channels },
	{ "LG Innotek TPI8PSB01N",
		{ CXM_TUNER_TV_SYSTEM_BG, cxm_none_system_code_style },
		48250, 855250,
		{ { 450000, { 0x8e, 0x30 } },
		  { 170000, { 0x8e, 0x90 } },
		  { 48250, { 0x8e, 0xa0 } } },
		87500, 108000,
		{ 87500, { 0x88, 0xa5 } },
		&bg_air_channels },
	{ "LG Innotek TAPC-H701F",
		{ CXM_TUNER_TV_SYSTEM_MN, cxm_none_system_code_style },
		55250, 801250,
		{ { 450000, { 0xce, 0x08 } },
		  { 165000, { 0xce, 0x02 } },
		  { 55250, { 0xce, 0x01 } } },
		0, 0,
		{ 0 },
		&us_cable_channels },
	{ "LG Innotek TAPC-H001F",
		{ CXM_TUNER_TV_SYSTEM_MN, cxm_none_system_code_style },
		55250, 801250,
		{ { 450000, { 0xce, 0x08 } },
		  { 165000, { 0xce, 0x02 } },
		  { 55250, { 0xce, 0x01 } } },
		76000, 108000,
		{ 76000, { 0x88, 0x04 } },
		&us_cable_channels },
	{ "LG Innotek TAPE-H001F",
		{ CXM_TUNER_TV_SYSTEM_MN,
		  cxm_if_system_with_aux_code_style,
		  { { CXM_TUNER_TV_SYSTEM_MN,      { 0x16, 0x30, 0x44, 0x30 }},
		    { CXM_TUNER_FM_SYSTEM,         { 0x0a, 0x90, 0x20, 0x30 }}
		    } },
		48250, 801250,
		{ { 442000, { 0xce, 0x04 } },
		  { 160000, { 0xce, 0x02 } },
		  { 48250, { 0xce, 0x01 } } },
		88000, 108000,
		{ 88000, { 0x88, 0x19 } },
		&us_cable_channels },
	{ "Microtune 4049 FM5",
		{ CXM_TUNER_TV_SYSTEM_BG | CXM_TUNER_TV_SYSTEM_DK
		  | CXM_TUNER_TV_SYSTEM_I
		  | CXM_TUNER_TV_SYSTEM_L | CXM_TUNER_TV_SYSTEM_L_PRIME,
		  cxm_if_system_code_style,
		  { { CXM_TUNER_TV_SYSTEM_BG,      { 0xd4, 0x70, 0x09 } },
		    { CXM_TUNER_TV_SYSTEM_DK,      { 0xd4, 0x70, 0x0b } },
		    { CXM_TUNER_TV_SYSTEM_I,       { 0xd4, 0x70, 0x0a } },
		    { CXM_TUNER_TV_SYSTEM_L,       { 0xc4, 0x10, 0x0b } },
		    { CXM_TUNER_TV_SYSTEM_L_PRIME, { 0x84, 0x10, 0x13 } },
		    { CXM_TUNER_FM_SYSTEM,         { 0xdc, 0x10, 0x1d } } } },
		45750, 855250,
		{ { 464000, { 0x8e, 0x30 } },
		  { 141000, { 0x8e, 0x90 } },
		  { 45750, { 0x8e, 0xa0 } } },
		87500, 108100,
		{ 87500, { 0x88, 0xa4 } },
		&l_air_channels },
	{ "TCL 2002N-6A",
		{ CXM_TUNER_TV_SYSTEM_MN, cxm_none_system_code_style },
		55250, 801250,
		{ { 446000, { 0x8e, 0x08 } },
		  { 170000, { 0x8e, 0x02 } },
		  { 55250, { 0x8e, 0x01 } } },
		0, 0,
		{ 0 },
		&us_cable_channels },
};


/* Read from the tuner registers */
static int
cxm_tuner_read(device_t iicbus, int i2c_addr, char *buf, int len)
{
	int received;

	if (iicbus_start(iicbus, i2c_addr + 1, CXM_I2C_TIMEOUT) != 0)
		return -1;

	if (iicbus_read(iicbus, buf, len, &received, IIC_LAST_READ, 0) != 0)
		goto fail;

	iicbus_stop(iicbus);

	return received;

fail:
	iicbus_stop(iicbus);
	return -1;
}


/* Write to the tuner registers */
static int
cxm_tuner_write(device_t iicbus, int i2c_addr, const char *buf, int len)
{
	int sent;

	if (iicbus_start(iicbus, i2c_addr, CXM_I2C_TIMEOUT) != 0)
		return -1;

	if (iicbus_write(iicbus, buf, len, &sent, CXM_I2C_TIMEOUT) != 0)
		goto fail;

	iicbus_stop(iicbus);

	return sent;

fail:
	iicbus_stop(iicbus);
	return -1;
}


int
cxm_tuner_init(struct cxm_softc *sc)
{
	unsigned char status;
	int tuner_type;

	if (cxm_eeprom_init(sc) < 0)
		return -1;

	tuner_type = cxm_eeprom_tuner_type(sc);

	if (tuner_type < 0 || tuner_type >= NUM_ELEMENTS(cxm_tuners))
		return -1;

	sc->tuner = &cxm_tuners[tuner_type];
	sc->tuner_channels = sc->tuner->default_channels;
	sc->tuner_freq = 0;

	if (cxm_tuner_read(sc->iicbus, CXM_I2C_TUNER, &status, sizeof(status))
	    != sizeof(status))
		return -1;

	if (cxm_tuner_select_channel(sc, 4) < 0)
		return -1;

	device_printf(sc->dev, "%s tuner\n", sc->tuner->name);

	return 0;
}


int
cxm_tuner_select_channel_set(struct cxm_softc *sc, unsigned int channel_set)
{
	unsigned int i;

	if (!channel_set) {
		sc->tuner_channels = sc->tuner->default_channels;
		return 0;
	}

	for (i = 0; i < NUM_ELEMENTS(channel_sets); i++)
		if (channel_sets[i]->chnlset == channel_set)
			break;

	if (i >= NUM_ELEMENTS(channel_sets))
		return -1;

	if (!(sc->tuner->systems.supported & channel_sets[i]->system))
		return -1;

	sc->tuner_channels = channel_sets[i];

	return 0;
}


unsigned int
cxm_tuner_selected_channel_set(struct cxm_softc *sc)
{
	return sc->tuner_channels->chnlset;
}


int
cxm_tuner_select_frequency(struct cxm_softc *sc,
			    enum cxm_tuner_freq_type freq_type,
			    unsigned long freq)
{
	unsigned char msg[5];
	unsigned char aux;
	unsigned char pb;
	unsigned int i;
	unsigned int system;
	unsigned int tuner_msg_len;
	unsigned long N;
	unsigned long osc_freq;
	const struct cxm_tuner_band_code *band_code;
	const struct cxm_tuner_system_code *system_code;

	N = 0;
	aux = 0;
	pb = 0;

	system_code = NULL;

	tuner_msg_len = 4;

	if (sc->tuner->systems.code_style != cxm_none_system_code_style) {
		system = freq_type == cxm_tuner_fm_freq_type
			 ? CXM_TUNER_FM_SYSTEM : sc->tuner_channels->system;

		for (i = 0; i < NUM_ELEMENTS (sc->tuner->systems.codes); i++)
			if (sc->tuner->systems.codes[i].system & system)
				break;

		if (i >= NUM_ELEMENTS (sc->tuner->systems.codes))
			return -1;

		switch (sc->tuner->systems.code_style) {
		case cxm_port_system_code_style:
			pb = sc->tuner->systems.codes[i].codes[0];
			break;

		case cxm_if_system_with_aux_code_style:
			aux = sc->tuner->systems.codes[i].codes[3];
			tuner_msg_len = 5;

			/* FALLTHROUGH */

		case cxm_if_system_code_style:
			system_code = &sc->tuner->systems.codes[i];
			break;

		default:
			return -1;
		}
	}

	switch (freq_type) {
	case cxm_tuner_fm_freq_type:

		if (freq < sc->tuner->fm_min_freq
		    || freq > sc->tuner->fm_max_freq
		    || !sc->tuner->fm_band_code.freq)
			return -1;

		/*
		 * The Philips FM1216ME MK3 data sheet recommends
		 * first setting the tuner to TV mode at a high
		 * frequency (e.g. 150 MHz), then selecting the
		 * desired FM station in order to ensure that the
		 * tuning voltage does not stay locked at 0V.
		 */

		if (cxm_tuner_select_frequency(sc, cxm_tuner_tv_freq_type,
					       150000) < 0)
			return -1;

		/*
		 * N = { fRF(pc) + fIF(pc) } / step_size
		 *
		 * fRF = RF frequency in MHz
		 * fIF = Intermediate frequency in MHz (FM = 10.70 MHz)
		 * step_size = Step size in MHz (FM = 50 kHz)
		 */

		osc_freq = freq + 10700;

		N = (20 * osc_freq) / 1000;

		msg[0] = (unsigned char)(N >> 8);
		msg[1] = (unsigned char)N;
		msg[2] = sc->tuner->fm_band_code.codes[0];
		msg[3] = sc->tuner->fm_band_code.codes[1] | pb;
		msg[4] = aux;
		break;

	case cxm_tuner_tv_freq_type:

		if (freq < sc->tuner->min_freq
		    || freq > sc->tuner->max_freq)
			return -1;

		/*
		 * N = 16 * { fRF(pc) + fIF(pc) }
		 *
		 * fRF = RF frequency in MHz
		 * fIF = Intermediate frequency in MHz
		 *
		 * The data sheet doesn't state it, however
		 * this is probably the same equation as
		 * FM simply with 62.5 kHz as the step size.
		 */

		osc_freq = freq + sc->tuner_channels->if_freq;

		N = (16 * osc_freq) / 1000;

		for (band_code = sc->tuner->band_codes;
		     band_code->freq > freq; band_code++)
			;

		if (freq >= sc->tuner_freq) {
			msg[0] = (unsigned char)(N >> 8);
			msg[1] = (unsigned char)N;
			msg[2] = band_code->codes[0];
			msg[3] = band_code->codes[1] | pb;
		} else {
			msg[0] = band_code->codes[0];
			msg[1] = band_code->codes[1] | pb;
			msg[2] = (unsigned char)(N >> 8);
			msg[3] = (unsigned char)N;
		}
		msg[4] = aux;
		break;

	default:
		return -1;
	}

	if (N > 32767)
		return -1;

	if (cxm_tuner_write(sc->iicbus, CXM_I2C_TUNER, msg, tuner_msg_len)
			    != tuner_msg_len)
		return -1;

	/*
	 * Program the IF processing after the tuner since some tuners
	 * use the control byte to set the address of the IF.
	 */

	if (system_code) {
		msg[0] = 0x00;
		msg[1] = system_code->codes[0];
		msg[2] = system_code->codes[1];
		msg[3] = system_code->codes[2];

		if (cxm_tuner_write(sc->iicbus, CXM_I2C_TUNER_IF, msg, 4) != 4)
			return -1;
	}

	sc->tuner_freq = freq;

	return 0;
}


int
cxm_tuner_select_channel(struct cxm_softc *sc, unsigned int channel)
{
	unsigned long freq;
	const struct cxm_tuner_channel_assignment *assignments;
	const struct cxm_tuner_channels *channels;

	channels = sc->tuner_channels;

	if (!channels
	    || channel < channels->min_channel
	    || channel > channels->max_channel)
		return -1;

	for (assignments = channels->assignments;
	     assignments->channel > channel; assignments++)
		;

	if (!assignments->freq)
		return -1;

	freq = assignments->freq
	       + (channel - assignments->channel) * assignments->step;

	return cxm_tuner_select_frequency(sc, cxm_tuner_tv_freq_type, freq);
}


int
cxm_tuner_apply_afc(struct cxm_softc *sc)
{
	unsigned int i;
	int status;
	unsigned long freq;
	unsigned long max_offset;
	unsigned long original_freq;
	unsigned long prev_freq;
	unsigned long step_size;

	if (cxm_tuner_wait_for_lock(sc) != 1)
		return -1;

	original_freq = sc->tuner_freq;

	freq = sc->tuner_freq;
	prev_freq = 0;
	max_offset = 2000;
	step_size = 63;

	for (i = 0; i < (max_offset / step_size); i++) {
		status = cxm_tuner_status(sc);

		if (status == -1)
			break;

		if (!(status & CXM_TUNER_PHASE_LOCKED))
			break;

		switch (status & CXM_TUNER_AFC_MASK) {
		case CXM_TUNER_AFC_FREQ_CENTERED:
			return 0;

		case CXM_TUNER_AFC_FREQ_MINUS_125:
		case CXM_TUNER_AFC_FREQ_MINUS_62:
			freq -= step_size;
			break;

		case CXM_TUNER_AFC_FREQ_PLUS_62:
		case CXM_TUNER_AFC_FREQ_PLUS_125:
			freq += step_size;
			break;

		default:
			goto fail;
		}

		if (freq == prev_freq)
			return 0;
		prev_freq = sc->tuner_freq;

		if (cxm_tuner_select_frequency(sc, cxm_tuner_tv_freq_type,
					       freq) < 0)
			break;

		/*
		 * Delay long enough for the tuner to update its status.
		 */

		tsleep(&sc->iicbus, 0, "afc", hz / 10);
	}

fail:
	cxm_tuner_select_frequency(sc, cxm_tuner_tv_freq_type, original_freq);
	return -1;
}


int
cxm_tuner_is_locked(struct cxm_softc *sc)
{
	int status;

	status = cxm_tuner_status(sc);

	if (status == -1)
		return -1;

	return (status & CXM_TUNER_PHASE_LOCKED) ? 1 : 0;
}


int
cxm_tuner_wait_for_lock(struct cxm_softc *sc)
{
	unsigned int i;

	/*
	 * The data sheet states the maximum lock-in time
	 * is 150 ms using fast tuning ... unfortunately
	 * it doesn't state the maximum lock-in time using
	 * moderate tuning.  Hopefully 300 ms is enough.
	 */

	for (i = 0; i < 3; i++) {

		/*
		 * The frequency may have just changed (prior to
		 * cxm_tuner_wait_for_lock) so start with the delay
		 * to give the tuner a chance to update its status.
		 */

		tsleep(&sc->iicbus, 0, "tuner", hz / 10);

		switch (cxm_tuner_is_locked(sc)) {
		case 1:
			return 1;

		case 0:
			break;

		default:
			return -1;
		}
	}

	device_printf(sc->dev, "tuner failed to lock\n");

	return 0;
}


static const unsigned char afcmap[16] = {
	CXM_TUNER_AFC_FREQ_CENTERED,
	CXM_TUNER_AFC_FREQ_MINUS_62,
	CXM_TUNER_AFC_FREQ_MINUS_62,
	CXM_TUNER_AFC_FREQ_MINUS_62,
	CXM_TUNER_AFC_FREQ_MINUS_125,
	CXM_TUNER_AFC_FREQ_MINUS_125,
	CXM_TUNER_AFC_FREQ_MINUS_125,
	CXM_TUNER_AFC_FREQ_MINUS_125,
	CXM_TUNER_AFC_FREQ_PLUS_125,
	CXM_TUNER_AFC_FREQ_PLUS_125,
	CXM_TUNER_AFC_FREQ_PLUS_125,
	CXM_TUNER_AFC_FREQ_PLUS_125,
	CXM_TUNER_AFC_FREQ_PLUS_62,
	CXM_TUNER_AFC_FREQ_PLUS_62,
	CXM_TUNER_AFC_FREQ_PLUS_62,
	CXM_TUNER_AFC_FREQ_CENTERED
};

int
cxm_tuner_status(struct cxm_softc *sc)
{
	unsigned char status;

	if (cxm_tuner_read(sc->iicbus, CXM_I2C_TUNER, &status, sizeof(status))
	    != sizeof(status))
		return -1;

	if (sc->tuner->systems.code_style == cxm_if_system_code_style
	    || sc->tuner->systems.code_style
	       == cxm_if_system_with_aux_code_style) {
		unsigned char if_status;

		if (cxm_tuner_read(sc->iicbus, CXM_I2C_TUNER_IF,
				   &if_status, sizeof(if_status))
		    != sizeof(if_status))
			return -1;

		status &= ~CXM_TUNER_AFC_MASK;
		status |= afcmap[(if_status >> 1) & 0x0f];
	}

	return status;
}
