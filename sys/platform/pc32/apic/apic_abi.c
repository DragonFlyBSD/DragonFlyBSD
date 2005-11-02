/*
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * All code starting at the apic_finalize() procedure definition is:
 *
 * Copyright (c) 1996, by Steve Passe
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the developer may NOT be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 * $DragonFly: src/sys/platform/pc32/apic/apic_abi.c,v 1.2 2005/11/02 18:41:59 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/machintr.h>
#include <machine/smp.h>

#ifdef APIC_IO

extern void APIC_INTREN(int);
extern void APIC_INTRDIS(int);

static int apic_setvar(int, const void *);
static int apic_getvar(int, void *);
static void apic_finalize(void);

static int picmode;

struct machintr_abi MachIntrABI = {
	MACHINTR_APIC,
	APIC_INTRDIS,
	APIC_INTREN,
	apic_setvar,
	apic_getvar,
	apic_finalize
};

static int
apic_setvar(int varid, const void *buf)
{
    int error = 0;

    switch(varid) {
    case MACHINTR_VAR_PICMODE:
	picmode = *(const int *)buf;
	break;
    default:
	error = ENOENT;
	break;
    }
    return (error);
}

static int
apic_getvar(int varid, void *buf)
{
    int error = 0;

    switch(varid) {
    case MACHINTR_VAR_PICMODE:
	*(int *)buf = picmode;
	break;
    default:
	error = ENOENT;
	break;
    }
    return (error);
}

/*
 * Final configuration of the BSP's local APIC:
 *  - disable 'pic mode'.
 *  - disable 'virtual wire mode'.
 *  - enable NMI.
 */
static void
apic_finalize(void)
{
    u_char		byte;
    u_int32_t	temp;

    /* leave 'pic mode' if necessary */
    if (picmode) {
	outb(0x22, 0x70);	/* select IMCR */
	byte = inb(0x23);	/* current contents */
	byte |= 0x01;		/* mask external INTR */
	outb(0x23, byte);	/* disconnect 8259s/NMI */
    }

    /* mask lint0 (the 8259 'virtual wire' connection) */
    temp = lapic.lvt_lint0;
    temp |= APIC_LVT_M;		/* set the mask */
    lapic.lvt_lint0 = temp;

    /* setup lint1 to handle NMI */
    temp = lapic.lvt_lint1;
    temp &= ~APIC_LVT_M;		/* clear the mask */
    lapic.lvt_lint1 = temp;

    if (bootverbose)
	apic_dump("bsp_apic_configure()");
}

#endif

