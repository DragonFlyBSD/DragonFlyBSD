/*-
 * Copyright (c) 1997 Bruce Evans.
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
 * $FreeBSD: src/sys/i386/isa/ipl_funcs.c,v 1.32.2.5 2002/12/17 18:04:02 sam Exp $
 * $DragonFly: src/sys/i386/isa/Attic/ipl_funcs.c,v 1.10 2005/06/16 21:12:47 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <machine/ipl.h>
#include <machine/globaldata.h>
#include <machine/pcb.h>
#include <i386/isa/intr_machdep.h>

/*
 * Bits in the ipending bitmap variable must be set atomically because
 * ipending may be manipulated by interrupts or other cpu's without holding 
 * any locks.
 *
 * Note: setbits uses a locked or, making simple cases MP safe.
 */
#define DO_SETBITS(name, var, bits) 					\
void name(void)								\
{									\
	struct mdglobaldata *gd = mdcpu;				\
	atomic_set_int_nonlocked(var, bits);				\
	atomic_set_int_nonlocked(&gd->mi.gd_reqflags, RQF_INTPEND);	\
}									\

DO_SETBITS(setdelayed,   &gd->gd_ipending, loadandclear(&gd->gd_idelayed))

DO_SETBITS(setsoftcamnet,&gd->gd_ipending, SWI_CAMNET_PENDING)
DO_SETBITS(setsoftcambio,&gd->gd_ipending, SWI_CAMBIO_PENDING)
DO_SETBITS(setsoftclock, &gd->gd_ipending, SWI_CLOCK_PENDING)
DO_SETBITS(setsoftnet,   &gd->gd_ipending, SWI_NET_PENDING)
DO_SETBITS(setsofttty,   &gd->gd_ipending, SWI_TTY_PENDING)
DO_SETBITS(setsoftvm,	 &gd->gd_ipending, SWI_VM_PENDING)
DO_SETBITS(setsofttq,	 &gd->gd_ipending, SWI_TQ_PENDING)
DO_SETBITS(setsoftcrypto,&gd->gd_ipending, SWI_CRYPTO_PENDING)

DO_SETBITS(schedsoftcamnet, &gd->gd_idelayed, SWI_CAMNET_PENDING)
DO_SETBITS(schedsoftcambio, &gd->gd_idelayed, SWI_CAMBIO_PENDING)
DO_SETBITS(schedsoftnet, &gd->gd_idelayed, SWI_NET_PENDING)
DO_SETBITS(schedsofttty, &gd->gd_idelayed, SWI_TTY_PENDING)
DO_SETBITS(schedsoftvm,	 &gd->gd_idelayed, SWI_VM_PENDING)
DO_SETBITS(schedsofttq,	 &gd->gd_idelayed, SWI_TQ_PENDING)
/* YYY schedsoft what? */

unsigned
softclockpending(void)
{
	return ((mdcpu->gd_ipending | mdcpu->gd_fpending) & SWI_CLOCK_PENDING);
}

