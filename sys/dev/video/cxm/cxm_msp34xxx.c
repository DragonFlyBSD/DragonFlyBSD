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
 * Audio decoder routines for the Conexant MPEG-2 Codec driver.
 *
 * Ideally these routines should be implemented as a separate
 * driver which has a generic audio decoder interface so that
 * it's not necessary for each multimedia driver to re-invent
 * the wheel.
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

#include <dev/video/cxm/cxm.h>

#include <bus/iicbus/iiconf.h>
#include <bus/iicbus/iicbus.h>

#include "iicbb_if.h"


static const struct cxm_msp_command
msp34x5G_init = {
	5,
	{
		/* Enable Automatic Sound Select */
		{ CXM_MSP3400C_DEM, 0x0030, { 0x20, 0x03 } },
		/* SCART Prescale = 0 dB */
		{ CXM_MSP3400C_DFP, 0x000d, { 0x19, 0x00 } },
		/* FM / AM Prescale = 100 Khz and FM Matrix = Sound A Mono  */
		{ CXM_MSP3400C_DFP, 0x000e, { 0x24, 0x03 } },
		/* NICAM Prescale = 9 dB  */
		{ CXM_MSP3400C_DFP, 0x0010, { 0x5a, 0x00 } },
		/* Enable Automatic Standard Select */
		{ CXM_MSP3400C_DEM, 0x0020, { 0x00, 0x01 } },
	}
};

static const struct cxm_msp_command
msp34x5G_select_tuner = {
	3,
	{
		/* Loudspeaker Source = demodulator (St or A), Matrix = St */
		{ CXM_MSP3400C_DFP, 0x0008, { 0x03, 0x20 } },
		/* SCART1_L/R Source = demodulator (St or A), Matrix = St */
		{ CXM_MSP3400C_DFP, 0x000a, { 0x03, 0x20 } },
		/* DSP In = mute, SC1_OUT_L/R Source = SCART1_L/R */
		{ CXM_MSP3400C_DFP, 0x0013, { 0x0f, 0x20 } }
	}
};

static const struct cxm_msp_command
msp34x5D_init = {
	4,
	{
		/* Enable Automatic NICAM-FM/AM Switching */
		{ CXM_MSP3400C_DEM, 0x0021, { 0x00, 0x01 } },
		/* SCART Prescale = 0 dB */
		{ CXM_MSP3400C_DFP, 0x000d, { 0x19, 0x00 } },
		/* NICAM Prescale = 9 dB  */
		{ CXM_MSP3400C_DFP, 0x0010, { 0x5a, 0x00 } },
		/* Enable Automatic Standard Select */
		{ CXM_MSP3400C_DEM, 0x0020, { 0x00, 0x01 } },
	}
};

static const struct cxm_msp_command
msp34x5D_select_tuner = {
	5,
	{
		/* Loudspeaker Source = demodulator (NICAM), Matrix = St */
		{ CXM_MSP3400C_DFP, 0x0008, { 0x01, 0x20 } },
		/* SCART1_L/R Source = demodulator (NICAM), Matrix = St */
		{ CXM_MSP3400C_DFP, 0x000a, { 0x01, 0x20 } },
		/* FM / AM Prescale = 100 Khz and FM Matrix = No Matrix  */
		{ CXM_MSP3400C_DFP, 0x000e, { 0x24, 0x00 } },
		/* FM Deemphasis = 50 us */
		{ CXM_MSP3400C_DFP, 0x000f, { 0x00, 0x00 } },
		/* DSP In = mute, SC1_OUT_L/R Source = SCART1_L/R */
		{ CXM_MSP3400C_DFP, 0x0013, { 0x0f, 0x20 } }
	}
};

static const struct cxm_msp_command
msp34xxx_mute = {
	2,
	{
		/* Loudspeaker volume = mute */
		{ CXM_MSP3400C_DFP, 0x0000, { 0x00, 0x00 } },
		/* SC1_OUT_L/R volume = mute */
		{ CXM_MSP3400C_DFP, 0x0007, { 0x00, 0x01 } }
	}
};

static const struct cxm_msp_command
msp34xxx_unmute = {
	2,
	{
		/* Loudspeaker volume = 0 db */
		{ CXM_MSP3400C_DFP, 0x0000, { 0x73, 0x00 } },
		/* SC1_OUT_L/R volume = 0 db */
		{ CXM_MSP3400C_DFP, 0x0007, { 0x73, 0x01 } }
	}
};

static const struct cxm_msp_command
msp34xxx_select_fm = {
	3,
	{
		/* Loudspeaker Source = SCART, Matrix = STEREO */
		{ CXM_MSP3400C_DFP, 0x0008, { 0x02, 0x20 } },
		/* SCART1_L/R Source = SCART, Matrix = STEREO */
		{ CXM_MSP3400C_DFP, 0x000a, { 0x02, 0x20 } },
		/* DSP In = SC2_IN_L/R, SC1_OUT_L/R Source = SCART1_L/R */
		{ CXM_MSP3400C_DFP, 0x0013, { 0x0e, 0x00 } }
	}
};

static const struct cxm_msp_command
msp34xxx_select_line_in = {
	3,
	{
		/* Loudspeaker Source = SCART, Matrix = STEREO */
		{ CXM_MSP3400C_DFP, 0x0008, { 0x02, 0x20 } },
		/* SCART1_L/R Source = SCART, Matrix = STEREO */
		{ CXM_MSP3400C_DFP, 0x000a, { 0x02, 0x20 } },
		/* DSP In = SC1_IN_L/R, SC1_OUT_L/R Source = SCART1_L/R */
		{ CXM_MSP3400C_DFP, 0x0013, { 0x0c, 0x00 } }
	}
};


/* Reset the MSP or DPL chip */
static int
cxm_msp_dpl_reset(device_t iicbus, int i2c_addr)
{
	unsigned char msg[3];
	int sent;

	/* put into reset mode */
	msg[0] = 0x00;
	msg[1] = 0x80;
	msg[2] = 0x00;

	if (iicbus_start(iicbus, i2c_addr, CXM_I2C_TIMEOUT) != 0)
		return -1;

	if (iicbus_write(iicbus, msg, sizeof(msg), &sent, CXM_I2C_TIMEOUT) != 0
	    || sent != sizeof(msg))
		goto fail;

	iicbus_stop(iicbus);

	/* put back to operational mode */
	msg[0] = 0x00;
	msg[1] = 0x00;
	msg[2] = 0x00;

	if (iicbus_start(iicbus, i2c_addr, CXM_I2C_TIMEOUT) != 0)
		return -1;

	if (iicbus_write(iicbus, msg, sizeof(msg), &sent, CXM_I2C_TIMEOUT) != 0
	    || sent != sizeof(msg))
		goto fail;

	iicbus_stop(iicbus);

	return 0;

fail:
	iicbus_stop(iicbus);
	return -1;
}


/* Read from the MSP or DPL registers */
static int
cxm_msp_dpl_read(device_t iicbus, int i2c_addr,
		  unsigned char dev, unsigned int addr,
		  char *buf, int len)
{
	unsigned char msg[3];
	int received;
	int sent;

	msg[0] = (unsigned char)(dev + 1);
	msg[1] = (unsigned char)(addr >> 8);
	msg[2] = (unsigned char)addr;

	if (iicbus_start(iicbus, i2c_addr, CXM_I2C_TIMEOUT) != 0)
		return -1;

	if (iicbus_write(iicbus, msg, sizeof(msg), &sent, CXM_I2C_TIMEOUT) != 0
	    || sent != sizeof(msg))
		goto fail;

	if (iicbus_repeated_start(iicbus, i2c_addr + 1, CXM_I2C_TIMEOUT) != 0)
		goto fail;

	if (iicbus_read(iicbus, buf, len, &received, IIC_LAST_READ, 0) != 0)
		goto fail;

	iicbus_stop(iicbus);

	return received;

fail:
	iicbus_stop(iicbus);
	return -1;
}


/* Write to the MSP or DPL registers */
static int
cxm_msp_dpl_write(device_t iicbus, int i2c_addr,
		   unsigned char dev, unsigned int addr,
		   const char *buf, int len)
{
	unsigned char msg[3];
	int sent;

	msg[0] = dev;
	msg[1] = (unsigned char)(addr >> 8);
	msg[2] = (unsigned char)addr;

	if (iicbus_start(iicbus, i2c_addr, CXM_I2C_TIMEOUT) != 0)
		return -1;

	if (iicbus_write(iicbus, msg, sizeof(msg), &sent, CXM_I2C_TIMEOUT) != 0
	    || sent != sizeof(msg))
		goto fail;

	if (iicbus_write(iicbus, buf, len, &sent, CXM_I2C_TIMEOUT) != 0)
		goto fail;

	iicbus_stop(iicbus);

	return sent;

fail:
	iicbus_stop(iicbus);
	return -1;
}


int
cxm_msp_init(struct cxm_softc *sc)
{
	unsigned char rev1[2];
	unsigned char rev2[2];
	unsigned int i;
	unsigned int nsettings;
	const struct cxm_msp_setting *settings;

	if (cxm_msp_dpl_reset (sc->iicbus, CXM_I2C_MSP3400) < 0)
		return -1;

	if (cxm_msp_dpl_read(sc->iicbus, CXM_I2C_MSP3400, CXM_MSP3400C_DFP,
			     0x001e, rev1, sizeof(rev1)) != sizeof(rev1))
		return -1;

	if (cxm_msp_dpl_read(sc->iicbus, CXM_I2C_MSP3400, CXM_MSP3400C_DFP,
			     0x001f, rev2, sizeof(rev2)) != sizeof(rev2))
		return -1;

	ksnprintf(sc->msp_name, sizeof(sc->msp_name), "%c4%02d%c-%c%d",
		 ((rev1[1] >> 4) & 0x0f) + '3', rev2[0],
		 (rev1[1] & 0x0f) + '@', rev1[0] + '@', rev2[1] & 0x1f);

	/*
	 * MSP 34x5D, 34x5G, and MSP 44x8G are the
	 * only audio decoders currently supported.
	 */

	if (strncmp(&sc->msp_name[0], "34", 2) == 0
	    && strncmp(&sc->msp_name[3], "5D", 2) == 0)
	  ;
	else if (strncmp(&sc->msp_name[0], "34", 2) == 0
		 && strncmp(&sc->msp_name[3], "5G", 2) == 0)
	  ;
	else if (strncmp(&sc->msp_name[0], "44", 2) == 0
		 && strncmp(&sc->msp_name[3], "8G", 2) == 0)
	  ;
	else {
		device_printf(sc->dev, "unknown audio decoder MSP%s\n",
		    sc->msp_name);
		return -1;
	}

	nsettings = msp34x5G_init.nsettings;
	settings = msp34x5G_init.settings;
	if (sc->msp_name[4] == 'D') {
		nsettings = msp34x5D_init.nsettings;
		settings = msp34x5D_init.settings;
	  }

	for (i = 0; i < nsettings; i++)
		if (cxm_msp_dpl_write(sc->iicbus, CXM_I2C_MSP3400,
				      settings[i].dev, settings[i].addr,
				      settings[i].value,
				      sizeof(settings[i].value))
		    != sizeof(settings[i].value))
			return -1;

	if (cxm_msp_select_source(sc, cxm_tuner_source) < 0)
		return -1;

	device_printf(sc->dev, "MSP%s audio decoder\n", sc->msp_name);

	return 0;
}


int
cxm_msp_mute(struct cxm_softc *sc)
{
	unsigned int i;
	unsigned int nsettings;
	const struct cxm_msp_setting *settings;

	nsettings = msp34xxx_mute.nsettings;
	settings = msp34xxx_mute.settings;

	for (i = 0; i < nsettings; i++)
		if (cxm_msp_dpl_write(sc->iicbus, CXM_I2C_MSP3400,
				      settings[i].dev, settings[i].addr,
				      settings[i].value,
				      sizeof(settings[i].value))
		    != sizeof(settings[i].value))
			return -1;

	return 0;
}


int
cxm_msp_unmute(struct cxm_softc *sc)
{
	unsigned int i;
	unsigned int nsettings;
	const struct cxm_msp_setting *settings;

	nsettings = msp34xxx_unmute.nsettings;
	settings = msp34xxx_unmute.settings;

	for (i = 0; i < nsettings; i++)
		if (cxm_msp_dpl_write(sc->iicbus, CXM_I2C_MSP3400,
				      settings[i].dev, settings[i].addr,
				      settings[i].value,
				      sizeof(settings[i].value))
		    != sizeof(settings[i].value))
			return -1;

	return 0;
}


int
cxm_msp_is_muted(struct cxm_softc *sc)
{
	unsigned char volume[2];

	if (cxm_msp_dpl_read(sc->iicbus, CXM_I2C_MSP3400, CXM_MSP3400C_DFP,
			     0x0000, volume, sizeof(volume)) != sizeof(volume))
		return -1;

	return volume[0] == 0x00 || volume[0] == 0xff ? 1 : 0;
}


int
cxm_msp_select_source(struct cxm_softc *sc, enum cxm_source source)
{
	unsigned int i;
	unsigned int nsettings;
	const struct cxm_msp_setting *settings;

	switch (source) {
	case cxm_fm_source:
		nsettings = msp34xxx_select_fm.nsettings;
		settings = msp34xxx_select_fm.settings;
		break;

	case cxm_line_in_source_composite:
	case cxm_line_in_source_svideo:
		nsettings = msp34xxx_select_line_in.nsettings;
		settings = msp34xxx_select_line_in.settings;
		break;

	case cxm_tuner_source:
		nsettings = msp34x5G_select_tuner.nsettings;
		settings = msp34x5G_select_tuner.settings;
		if (sc->msp_name[4] == 'D') {
			nsettings = msp34x5D_select_tuner.nsettings;
			settings = msp34x5D_select_tuner.settings;
		  }
		break;

	default:
		return -1;
	}

	for (i = 0; i < nsettings; i++)
		if (cxm_msp_dpl_write(sc->iicbus, CXM_I2C_MSP3400,
				      settings[i].dev, settings[i].addr,
				      settings[i].value,
				      sizeof(settings[i].value))
		    != sizeof(settings[i].value))
			return -1;

	return 0;
}


enum cxm_source
cxm_msp_selected_source(struct cxm_softc *sc)
{
	unsigned char dsp[2];
	unsigned char source[2];

	if (cxm_msp_dpl_read(sc->iicbus, CXM_I2C_MSP3400, CXM_MSP3400C_DFP,
			     0x0008, source, sizeof(source)) != sizeof(source))
		return cxm_unknown_source;

	switch (source[0]) {
	case 0: /* FM / AM mono signal */
	case 1: /* Stereo or A / B */
	case 3: /* Stereo or A */
	case 4: /* Stereo or B */
		return cxm_tuner_source;

	case 2: /* SCART */
		break;

	default:
		return cxm_unknown_source;
	}

	if (cxm_msp_dpl_read(sc->iicbus, CXM_I2C_MSP3400, CXM_MSP3400C_DFP,
			     0x0013, dsp, sizeof(dsp)) != sizeof(dsp))
		return cxm_unknown_source;

	if (dsp[1] & 0x20)
		return cxm_unknown_source;

	switch (dsp[0] & 0x03) {
	case 0:
		return cxm_line_in_source_composite;

	case 2:
		return cxm_fm_source;

	default:
		 return cxm_unknown_source;
	}
}


int
cxm_msp_autodetect_standard(struct cxm_softc *sc)
{
	unsigned int i;
	int locked;
	unsigned int nsettings;
	const struct cxm_msp_setting *settings;

	switch (cxm_msp_selected_source(sc)) {
	case cxm_tuner_source:
		break;

	case cxm_fm_source:
	case cxm_line_in_source_composite:
	case cxm_line_in_source_svideo:
		return 1;

	default:
		return -1;
	}

	/*
	 * Section 3.3.2.2 of the data sheet states:
	 *
	 *   A general refresh of the STANDARD SELECT
	 *   register is not allowed.
	 */

	if (cxm_msp_dpl_reset (sc->iicbus, CXM_I2C_MSP3400) < 0)
		return -1;

	nsettings = msp34x5G_init.nsettings;
	settings = msp34x5G_init.settings;
	if (sc->msp_name[4] == 'D') {
		nsettings = msp34x5D_init.nsettings;
		settings = msp34x5D_init.settings;
	  }

	for (i = 0; i < nsettings; i++)
		if (cxm_msp_dpl_write(sc->iicbus, CXM_I2C_MSP3400,
				      settings[i].dev, settings[i].addr,
				      settings[i].value,
				      sizeof(settings[i].value))
		    != sizeof(settings[i].value))
			return -1;

	locked = cxm_msp_wait_for_lock(sc);

	if (cxm_msp_select_source(sc, cxm_tuner_source) < 0)
		return -1;

	return locked;
}


int
cxm_msp_is_locked(struct cxm_softc *sc)
{
	unsigned char source[2];
	unsigned char standard[2];

	if (cxm_msp_dpl_read(sc->iicbus, CXM_I2C_MSP3400, CXM_MSP3400C_DFP,
			     0x0008, source, sizeof(source)) != sizeof(source))
		return -1;

	switch (source[0]) {
	case 0: /* FM / AM mono signal */
	case 1: /* Stereo or A / B */
	case 3: /* Stereo or A */
	case 4: /* Stereo or B */
		break;

	default:
		return 1;
	}

	if (cxm_msp_dpl_read(sc->iicbus, CXM_I2C_MSP3400, CXM_MSP3400C_DEM,
			     0x007e, standard, sizeof(standard))
	    != sizeof(standard))
		return -1;

	if (standard[0] >= 8 || (standard[0] == 0 && standard[1] == 0))
		return 0;

	return 1;
}


int
cxm_msp_wait_for_lock(struct cxm_softc *sc)
{
	unsigned int i;

	/*
	 * Section 3.3.2.1 of the data sheet states:
	 *
	 *   Within 0.5 s the detection and setup of the actual
	 *   TV sound standard is performed.  The detected result
	 *   can be read out of the STANDARD RESULT register by
	 *   the control processor.
	 */

	for (i = 0; i < 10; i++) {

		/*
		 * The input may have just changed (prior to
		 * cxm_msp_wait_for_lock) so start with the
		 * delay to give the audio decoder a chance
		 * to update its status.
		 */

		tsleep(&sc->iicbus, 0, "audio", hz / 20);

		switch (cxm_msp_is_locked(sc)) {
		case 1:
			return 1;

		case 0:
			break;

		default:
			return -1;
		}
	}

	device_printf(sc->dev, "audio decoder failed to lock\n");

	return 0;
}
