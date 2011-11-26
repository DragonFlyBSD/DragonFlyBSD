/*-
 * Copyright (c) 1998 Nicolas Souchu
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/iicbus/iicbus.h,v 1.8 2008/08/04 21:03:06 jhb Exp $
 * $DragonFly: src/sys/bus/iicbus/iicbus.h,v 1.3 2006/09/30 20:03:44 swildner Exp $
 *
 */
#ifndef __IICBUS_H
#define __IICBUS_H

#define IICBUS_IVAR(d) (struct iicbus_ivar *) device_get_ivars(d)
#define IICBUS_SOFTC(d) (struct iicbus_softc *) device_get_softc(d)

struct iicbus_softc
{
	device_t dev;		/* Myself */
	device_t owner;		/* iicbus owner device structure */
	u_char started;		/* address of the 'started' slave
				 * 0 if no start condition succeeded */
};

struct iicbus_ivar
{
	uint32_t	addr;
};

enum {
	IICBUS_IVAR_ADDR		/* Address or base address */
};

#define IICBUS_ACCESSOR(A, B, T)					\
	__BUS_ACCESSOR(iicbus, A, IICBUS, B, T)

IICBUS_ACCESSOR(addr,		ADDR,		uint32_t)

#define	IICBUS_LOCK(sc)
#define	IICBUS_UNLOCK(sc)
#define	IICBUS_ASSERT_LOCKED(sc)

extern int iicbus_generic_intr(device_t dev, int event, char *buf);

extern driver_t iicbus_driver;
extern devclass_t iicbus_devclass;

#endif
