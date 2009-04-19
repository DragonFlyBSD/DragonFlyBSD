/*
 * Copyright (c) 2003, 2004
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
 * Infrared remote routines for the Conexant MPEG-2 Codec driver.
 *
 * Ideally these routines should be implemented as a separate
 * driver which has a generic infrared remote interface so that
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


static int
cxm_ir_read(device_t iicbus, int i2c_addr, char *buf, int len)
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


int
cxm_ir_init(struct cxm_softc *sc)
{
	unsigned char key[1];

	if (cxm_ir_read(sc->iicbus, CXM_I2C_IR,
			    key, sizeof(key)) != sizeof(key))
		return -1;

	device_printf(sc->dev, "IR Remote\n");

	return 0;
}


int
cxm_ir_key(struct cxm_softc *sc, char *buf, int len)
{
	int result;

	result = cxm_ir_read(sc->iicbus, CXM_I2C_IR, buf, len);

	if (result >= 0)
		return result;

	/*
	 * If the IR receiver didn't respond,
	 * then wait 50 ms and try again.
	 */

	tsleep(&sc->iicbus, 0, "IR", hz / 20);

	return cxm_ir_read(sc->iicbus, CXM_I2C_IR, buf, len);
}
