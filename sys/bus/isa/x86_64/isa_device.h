/*-
 * Copyright (c) 1991 The Regents of the University of California.
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
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)isa_device.h	7.1 (Berkeley) 5/9/91
 * $FreeBSD: src/sys/i386/isa/isa_device.h,v 1.68 2000/01/29 18:01:10 peter Exp $
 */

#ifndef _BUS_ISA_ARCH_ISA_DEVICE_H_
#define	_BUS_ISA_ARCH_ISA_DEVICE_H_

#ifndef _SYS_INTERRUPT_H_
#include <sys/interrupt.h>
#endif

/*
 * ISA Bus Autoconfiguration
 */

/*
 * Per device structure.
 */
struct isa_device {
	int	id_id;		/* device id */
	struct	isa_driver *id_driver;
	int	id_iobase;	/* base i/o address */
	int	id_iosize;	/* base i/o length */
	u_int	id_irq;		/* interrupt request */
	int	id_drq;		/* DMA request */
	caddr_t id_maddr;	/* physical i/o memory address on bus (if any)*/
	int	id_msize;	/* size of i/o memory */
	inthand2_t *id_intr;
	int	id_unit;	/* unit number */
	int	id_flags;	/* flags */
	int	id_enabled;	/* is device enabled */
	struct isa_device *id_next; /* used in isa_devlist in userconfig() */
	device_t id_device; /* new-bus wrapper device */
};

/*
 * Per-driver structure.
 *
 * Each device driver defines entries for a set of routines
 * as well as an array of types which are acceptable to it.
 * These are used at boot time by the configuration program.
 */
struct isa_driver {
	int	(*probe) (struct isa_device *idp);
					/* test whether device is present */
	int	(*attach) (struct isa_device *idp);
					/* setup driver for a device */
	char	*name;			/* device name */
	int	sensitive_hw;		/* true if other probes confuse us */
};

#endif /* !_BUS_ISA_ARCH_ISA_DEVICE_H_ */
