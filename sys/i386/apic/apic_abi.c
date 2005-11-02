/*
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
 * Copyright (c) 1996, by Steve Passe.  All rights reserved.
 * Copyright (c) 1991 The Regents of the University of California.
 * All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
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
 * $DragonFly: src/sys/i386/apic/Attic/apic_abi.c,v 1.4 2005/11/02 22:59:42 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/machintr.h>
#include <sys/interrupt.h>
#include <sys/bus.h>
#include <machine/smptests.h>
#include <machine/smp.h>
#include <machine/segments.h>
#include <machine/md_var.h>
#include <machine/clock.h>	/* apic_8254_intr */
#include <i386/isa/intr_machdep.h>
#include <i386/icu/icu.h>
#include "apic_ipl.h"

#ifdef APIC_IO

extern void APIC_INTREN(int);
extern void APIC_INTRDIS(int);

extern inthand_t
	IDTVEC(apic_fastintr0), IDTVEC(apic_fastintr1),
	IDTVEC(apic_fastintr2), IDTVEC(apic_fastintr3),
	IDTVEC(apic_fastintr4), IDTVEC(apic_fastintr5),
	IDTVEC(apic_fastintr6), IDTVEC(apic_fastintr7),
	IDTVEC(apic_fastintr8), IDTVEC(apic_fastintr9),
	IDTVEC(apic_fastintr10), IDTVEC(apic_fastintr11),
	IDTVEC(apic_fastintr12), IDTVEC(apic_fastintr13),
	IDTVEC(apic_fastintr14), IDTVEC(apic_fastintr15),
	IDTVEC(apic_fastintr16), IDTVEC(apic_fastintr17),
	IDTVEC(apic_fastintr18), IDTVEC(apic_fastintr19),
	IDTVEC(apic_fastintr20), IDTVEC(apic_fastintr21),
	IDTVEC(apic_fastintr22), IDTVEC(apic_fastintr23);

extern inthand_t
	IDTVEC(apic_slowintr0), IDTVEC(apic_slowintr1),
	IDTVEC(apic_slowintr2), IDTVEC(apic_slowintr3),
	IDTVEC(apic_slowintr4), IDTVEC(apic_slowintr5),
	IDTVEC(apic_slowintr6), IDTVEC(apic_slowintr7),
	IDTVEC(apic_slowintr8), IDTVEC(apic_slowintr9),
	IDTVEC(apic_slowintr10), IDTVEC(apic_slowintr11),
	IDTVEC(apic_slowintr12), IDTVEC(apic_slowintr13),
	IDTVEC(apic_slowintr14), IDTVEC(apic_slowintr15),
	IDTVEC(apic_slowintr16), IDTVEC(apic_slowintr17),
	IDTVEC(apic_slowintr18), IDTVEC(apic_slowintr19),
	IDTVEC(apic_slowintr20), IDTVEC(apic_slowintr21),
	IDTVEC(apic_slowintr22), IDTVEC(apic_slowintr23);

extern unpendhand_t
	IDTVEC(fastunpend0), IDTVEC(fastunpend1),
	IDTVEC(fastunpend2), IDTVEC(fastunpend3),
	IDTVEC(fastunpend4), IDTVEC(fastunpend5),
	IDTVEC(fastunpend6), IDTVEC(fastunpend7),
	IDTVEC(fastunpend8), IDTVEC(fastunpend9),
	IDTVEC(fastunpend10), IDTVEC(fastunpend11),
	IDTVEC(fastunpend12), IDTVEC(fastunpend13),
	IDTVEC(fastunpend14), IDTVEC(fastunpend15),
	IDTVEC(fastunpend16), IDTVEC(fastunpend17),
	IDTVEC(fastunpend18), IDTVEC(fastunpend19),
	IDTVEC(fastunpend20), IDTVEC(fastunpend21),
	IDTVEC(fastunpend22), IDTVEC(fastunpend23);

extern inthand_t
	IDTVEC(apic_wrongintr0), IDTVEC(apic_wrongintr1),
	IDTVEC(apic_wrongintr2), IDTVEC(apic_wrongintr3),
	IDTVEC(apic_wrongintr4), IDTVEC(apic_wrongintr5),
	IDTVEC(apic_wrongintr6), IDTVEC(apic_wrongintr7),
	IDTVEC(apic_wrongintr8), IDTVEC(apic_wrongintr9),
	IDTVEC(apic_wrongintr10), IDTVEC(apic_wrongintr11),
	IDTVEC(apic_wrongintr12), IDTVEC(apic_wrongintr13),
	IDTVEC(apic_wrongintr14), IDTVEC(apic_wrongintr15),
	IDTVEC(apic_wrongintr16), IDTVEC(apic_wrongintr17),
	IDTVEC(apic_wrongintr18), IDTVEC(apic_wrongintr19),
	IDTVEC(apic_wrongintr20), IDTVEC(apic_wrongintr21),
	IDTVEC(apic_wrongintr22), IDTVEC(apic_wrongintr23);

static int apic_setvar(int, const void *);
static int apic_getvar(int, void *);
static int apic_vectorctl(int, int, int);
static void apic_finalize(void);

static inthand_t *apic_fastintr[APIC_HWI_VECTORS] = {
	&IDTVEC(apic_fastintr0), &IDTVEC(apic_fastintr1),
	&IDTVEC(apic_fastintr2), &IDTVEC(apic_fastintr3),
	&IDTVEC(apic_fastintr4), &IDTVEC(apic_fastintr5),
	&IDTVEC(apic_fastintr6), &IDTVEC(apic_fastintr7),
	&IDTVEC(apic_fastintr8), &IDTVEC(apic_fastintr9),
	&IDTVEC(apic_fastintr10), &IDTVEC(apic_fastintr11),
	&IDTVEC(apic_fastintr12), &IDTVEC(apic_fastintr13),
	&IDTVEC(apic_fastintr14), &IDTVEC(apic_fastintr15),
	&IDTVEC(apic_fastintr16), &IDTVEC(apic_fastintr17),
	&IDTVEC(apic_fastintr18), &IDTVEC(apic_fastintr19),
	&IDTVEC(apic_fastintr20), &IDTVEC(apic_fastintr21),
	&IDTVEC(apic_fastintr22), &IDTVEC(apic_fastintr23)
};

static inthand_t *apic_slowintr[APIC_HWI_VECTORS] = {
	&IDTVEC(apic_slowintr0), &IDTVEC(apic_slowintr1),
	&IDTVEC(apic_slowintr2), &IDTVEC(apic_slowintr3),
	&IDTVEC(apic_slowintr4), &IDTVEC(apic_slowintr5),
	&IDTVEC(apic_slowintr6), &IDTVEC(apic_slowintr7),
	&IDTVEC(apic_slowintr8), &IDTVEC(apic_slowintr9),
	&IDTVEC(apic_slowintr10), &IDTVEC(apic_slowintr11),
	&IDTVEC(apic_slowintr12), &IDTVEC(apic_slowintr13),
	&IDTVEC(apic_slowintr14), &IDTVEC(apic_slowintr15),
	&IDTVEC(apic_slowintr16), &IDTVEC(apic_slowintr17),
	&IDTVEC(apic_slowintr18), &IDTVEC(apic_slowintr19),
	&IDTVEC(apic_slowintr20), &IDTVEC(apic_slowintr21),
	&IDTVEC(apic_slowintr22), &IDTVEC(apic_slowintr23)
};

unpendhand_t *fastunpend[APIC_HWI_VECTORS] = {
	IDTVEC(fastunpend0), IDTVEC(fastunpend1),
	IDTVEC(fastunpend2), IDTVEC(fastunpend3),
	IDTVEC(fastunpend4), IDTVEC(fastunpend5),
	IDTVEC(fastunpend6), IDTVEC(fastunpend7),
	IDTVEC(fastunpend8), IDTVEC(fastunpend9),
	IDTVEC(fastunpend10), IDTVEC(fastunpend11),
	IDTVEC(fastunpend12), IDTVEC(fastunpend13),
	IDTVEC(fastunpend14), IDTVEC(fastunpend15),
	IDTVEC(fastunpend16), IDTVEC(fastunpend17),
	IDTVEC(fastunpend18), IDTVEC(fastunpend19),
	IDTVEC(fastunpend20), IDTVEC(fastunpend21),
	IDTVEC(fastunpend22), IDTVEC(fastunpend23)
};

static inthand_t *apic_wrongintr[APIC_HWI_VECTORS] = {
	&IDTVEC(apic_wrongintr0), &IDTVEC(apic_wrongintr1),
	&IDTVEC(apic_wrongintr2), &IDTVEC(apic_wrongintr3),
	&IDTVEC(apic_wrongintr4), &IDTVEC(apic_wrongintr5),
	&IDTVEC(apic_wrongintr6), &IDTVEC(apic_wrongintr7),
	&IDTVEC(apic_wrongintr8), &IDTVEC(apic_wrongintr9),
	&IDTVEC(apic_wrongintr10), &IDTVEC(apic_wrongintr11),
	&IDTVEC(apic_wrongintr12), &IDTVEC(apic_wrongintr13),
	&IDTVEC(apic_wrongintr14), &IDTVEC(apic_wrongintr15),
	&IDTVEC(apic_wrongintr16), &IDTVEC(apic_wrongintr17),
	&IDTVEC(apic_wrongintr18), &IDTVEC(apic_wrongintr19),
	&IDTVEC(apic_wrongintr20), &IDTVEC(apic_wrongintr21),
	&IDTVEC(apic_wrongintr22), &IDTVEC(apic_wrongintr23)
};

static int picmode;

struct machintr_abi MachIntrABI = {
	MACHINTR_APIC,
	APIC_INTRDIS,
	APIC_INTREN,
	apic_vectorctl,
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

static
int
apic_vectorctl(int op, int intr, int flags)
{
    int error;
    int vector;
    int select;
    u_int32_t value;
    u_long ef;

    if (intr < 0 || intr >= APIC_HWI_VECTORS)
	return (EINVAL);

    ef = read_eflags();
    cpu_disable_intr();
    error = 0;

    switch(op) {
    case MACHINTR_VECTOR_SETUP:
	if (flags & INTR_FAST) {
	    vector = TPR_SLOW_INTS + intr;
	    setidt(vector, apic_wrongintr[intr],
		    SDT_SYS386IGT, SEL_KPL, GSEL(GCODE_SEL, SEL_KPL));
	    vector = TPR_FAST_INTS + intr;
	    setidt(vector, apic_fastintr[intr],
		    SDT_SYS386IGT, SEL_KPL, GSEL(GCODE_SEL, SEL_KPL));
	} else {
	    vector = TPR_SLOW_INTS + intr;
#if defined(APIC_INTR_REORDER) && defined(APIC_INTR_HIGHPRI_CLOCK)
	    /* XXX: Hack (kludge?) for more accurate clock. */
	    if (intr == apic_8254_intr || intr == 8) {
		vector = TPR_FAST_INTS + intr;
	    }
	    setidt(vector, apic_slowintr[intr],
		    SDT_SYS386IGT, SEL_KPL, GSEL(GCODE_SEL, SEL_KPL));
#endif
	}
#ifdef APIC_INTR_REORDER
        set_lapic_isrloc(intr, vector);
#endif
	/*
	 * Reprogram the vector in the IO APIC.
	 *
	 * XXX EOI/mask a pending (stray) interrupt on the old vector?
	 */
	if (int_to_apicintpin[intr].ioapic >= 0) {
	    select = int_to_apicintpin[intr].redirindex;
	    value = io_apic_read(int_to_apicintpin[intr].ioapic,
				 select) & ~IOART_INTVEC;
	    io_apic_write(int_to_apicintpin[intr].ioapic,
			  select, value | vector);
	}
	machintr_intren(intr);
	break;
    case MACHINTR_VECTOR_TEARDOWN:
	machintr_intrdis(intr);
#ifdef APIC_INTR_REORDER
	set_lapic_isrloc(intr, IDT_OFFSET + intr);
#endif
	setidt(IDT_OFFSET + intr, apic_slowintr[intr], SDT_SYS386IGT, SEL_KPL,
		GSEL(GCODE_SEL, SEL_KPL));
	break;
    default:
	error = EOPNOTSUPP;
	break;
    }

    write_eflags(ef);
    return (error);
}

#endif

