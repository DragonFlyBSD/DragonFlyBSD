/*-
 * Copyright (c) 1993 The Regents of the University of California.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 * $FreeBSD: src/sys/i386/include/ipl.h,v 1.17.2.3 2002/12/17 18:04:02 sam Exp $
 * $DragonFly: src/sys/i386/include/Attic/ipl.h,v 1.9 2005/11/04 08:57:29 dillon Exp $
 */

#ifndef _MACHINE_IPL_H_
#define	_MACHINE_IPL_H_

#include <i386/apic/apic_ipl.h>
#include <i386/icu/icu_ipl.h>

/*
 * Software interrupt bit numbers in priority order.  The priority only
 * determines which swi will be dispatched next; a higher priority swi
 * may be dispatched when a nested h/w interrupt handler returns.
 */
#define	SWI_TTY		(FIRST_SOFTINT + 0)
#define	SWI_NET		(FIRST_SOFTINT + 1)
#define	SWI_CAMNET	(FIRST_SOFTINT + 2)
#define	SWI_CRYPTO	SWI_CAMNET
#define	SWI_CAMBIO	(FIRST_SOFTINT + 3)
#define	SWI_VM		(FIRST_SOFTINT + 4)
#define	SWI_TQ		(FIRST_SOFTINT + 5)
#define	SWI_CLOCK	(FIRST_SOFTINT + 6)

/*
 * Corresponding interrupt-pending bits for spending.  NOTE: i386 only
 * supports 32 software interupts (due to its gd_spending mask).
 */
#define	SWI_TTY_PENDING		(1 << (SWI_TTY - FIRST_SOFTINT))
#define	SWI_NET_PENDING		(1 << (SWI_NET - FIRST_SOFTINT))
#define	SWI_CAMNET_PENDING	(1 << (SWI_CAMNET - FIRST_SOFTINT))
#define	SWI_CRYPTO_PENDING	SWI_CAMNET_PENDING
#define	SWI_CAMBIO_PENDING	(1 << (SWI_CAMBIO - FIRST_SOFTINT))
#define	SWI_VM_PENDING		(1 << (SWI_VM - FIRST_SOFTINT))
#define	SWI_TQ_PENDING		(1 << (SWI_TQ - FIRST_SOFTINT))
#define	SWI_CLOCK_PENDING	(1 << (SWI_CLOCK - FIRST_SOFTINT))

#ifndef	LOCORE

#endif /* !LOCORE */

#endif /* !_MACHINE_IPL_H_ */
