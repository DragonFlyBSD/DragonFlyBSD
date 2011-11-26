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
 * EEPROM routines for the Conexant MPEG-2 Codec driver.
 *
 * Ideally these routines should be implemented as a separate
 * driver which has a generic EEPROM interface so that it's
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

#include <dev/video/cxm/cxm.h>

#include <bus/iicbus/iiconf.h>
#include <bus/iicbus/iicbus.h>

#include "iicbb_if.h"


static int
cxm_eeprom_read(device_t iicbus, int i2c_addr,
		 char *buf, int len, unsigned int offset)
{
	char msg[1];
	int received;
	int sent;

	msg[0] = (unsigned char)offset;

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


int
cxm_eeprom_init(struct cxm_softc *sc)
{
	unsigned char eeprom[1];

	if (cxm_eeprom_read(sc->iicbus, CXM_I2C_EEPROM,
			    eeprom, sizeof(eeprom), 0) != sizeof(eeprom))
		return -1;

	return 0;
}


int
cxm_eeprom_tuner_type(struct cxm_softc *sc)
{
	unsigned char eeprom[256];
	unsigned int i;
	unsigned int len;
	unsigned int subsystem_vendor_id;
	unsigned int tuner_code;
	int tuner_type;

	if (cxm_eeprom_read(sc->iicbus, CXM_I2C_EEPROM,
			    eeprom, sizeof(eeprom), 0) != sizeof(eeprom))
		return -1;

	subsystem_vendor_id = (unsigned int)eeprom[254] << 8 | eeprom[255];
	tuner_type = -1;

	switch (subsystem_vendor_id) {
	case PCI_VENDOR_HAUPPAUGE:

		/*
		 * The Hauppauge eeprom format is tagged.
		 */

		if (eeprom[0] != 0x84) {
			device_printf(sc->dev,
			    "unknown Hauppauge eeprom format %#x\n",
			    (unsigned int)eeprom[0]);
			break;
		}

		tuner_code = -1;

		for (i = 0; i < sizeof(eeprom); i += len) {
			len = 0;
			if (eeprom[i] == 0x84) {
				len = (unsigned int)eeprom[i + 2] << 8
				      | eeprom[i + 1];
				i += 3;
			} else if ((eeprom[i] & 0xf0) == 0x70) {
				if (eeprom[i] & 0x08)
					break;
				len = eeprom[i] & 0x07;
				i++;
			} else {
				device_printf(sc->dev,
				    "unknown Hauppauge eeprom packet %#x\n",
				    (unsigned int)eeprom[i]);
				return -1;
			}

			if (i >= sizeof(eeprom)
			    || (i + len) > sizeof(eeprom)) {
				device_printf(sc->dev,
				    "corrupt Hauppauge eeprom packet\n");
				return -1;
			}

			switch (eeprom[i]) {
			case 0x00:
				tuner_code = eeprom[i + 6];
				break;

			case 0x0a:
				tuner_code = eeprom[i + 2];
				break;

			default:
				break;
			}
		}

		switch (tuner_code) {
		case 0x03: /* Philips FI1216 */
		case 0x08: /* Philips FI1216 MK2 */
			tuner_type = CXM_TUNER_PHILIPS_FI1216_MK2;
			break;

		case 0x22: /* Philips FQ1216ME */
			tuner_type = CXM_TUNER_PHILIPS_FQ1216ME;
			break;

		case 0x37: /* Philips FQ1216ME MK3 */
			tuner_type = CXM_TUNER_PHILIPS_FQ1216ME_MK3;
			break;

		case 0x1d: /* Temic 4006FH5 */
			tuner_type = CXM_TUNER_TEMIC_4006_FH5;
			break;

		case 0x30: /* LG Innotek TPI8PSB11D */
			tuner_type = CXM_TUNER_LG_TPI8PSB11D;
			break;

		case 0x34: /* Microtune 4049FM5 */
			tuner_type = CXM_TUNER_MICROTUNE_4049_FM5;
			break;

		case 0x05: /* Philips FI1236 */
		case 0x0a: /* Philips FI1236 MK2 */
			tuner_type = CXM_TUNER_PHILIPS_FI1236_MK2;
			break;

		case 0x1a: /* Temic 4036FY5 */
			tuner_type = CXM_TUNER_TEMIC_4036_FY5;
			break;

		case 0x52: /* LG Innotek TAPC-H701F */
			tuner_type = CXM_TUNER_LG_TAPC_H701F;
			break;

		case 0x55: /* TCL 2002N-6A */
			tuner_type = CXM_TUNER_TCL_2002N_6A;
			break;

		case 0x06: /* Philips FI1246 */
		case 0x0b: /* Philips FI1246 MK2 */
			tuner_type = CXM_TUNER_PHILIPS_FI1246_MK2;
			break;

		case 0x23: /* Temic 4066FY5 */
			tuner_type = CXM_TUNER_TEMIC_4066_FY5;
			break;

		case 0x10: /* Philips FR1216 MK2 */
		case 0x15: /* Philips FM1216 */
			tuner_type = CXM_TUNER_PHILIPS_FM1216;
			break;

		case 0x39: /* Philips FM1216ME MK3 */
			tuner_type = CXM_TUNER_PHILIPS_FM1216ME_MK3;
			break;

		case 0x2a: /* Temic 4009FR5 */
			tuner_type = CXM_TUNER_TEMIC_4009_FR5;
			break;

		case 0x2f: /* LG Innotek TPI8PSB01N */
			tuner_type = CXM_TUNER_LG_TPI8PSB01N;
			break;

		case 0x12: /* Philips FR1236 MK2 */
		case 0x17: /* Philips FM1236 */
			tuner_type = CXM_TUNER_PHILIPS_FM1236;
			break;

		case 0x21: /* Temic 4039FR5 */
			tuner_type = CXM_TUNER_TEMIC_4039_FR5;
			break;

		case 0x44: /* LG Innotek TAPE-H001F */
			tuner_type = CXM_TUNER_LG_TAPE_H001F;
			break;

		case 0x13: /* Philips FR1246 MK2 */
		case 0x18: /* Philips FM1246 */
			tuner_type = CXM_TUNER_PHILIPS_FM1246;
			break;

		default:
			device_printf(sc->dev, "unknown tuner code %#x\n",
			    tuner_code);
			break;
		}
		break;

	default:
		device_printf(sc->dev, "unknown subsystem vendor id %#x\n",
		    subsystem_vendor_id);
		break;
	}

	return tuner_type;
}
