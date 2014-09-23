/*-
 * Copyright (c) 1998 Michael Smith <msmith@freebsd.org>
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
 * $FreeBSD: src/sys/boot/i386/libi386/biosmem.c,v 1.7 2003/08/25 23:28:31 obrien Exp $
 * $DragonFly: src/sys/boot/pc32/libi386/biosmem.c,v 1.4 2004/06/26 02:26:22 dillon Exp $
 */
/*
 * Obtain memory configuration information from the BIOS
 */
#include <stand.h>
#include "libi386.h"
#include "btxv86.h"

vm_offset_t	memtop;
u_int32_t	bios_basemem, bios_extmem, bios_howmem;

#define SMAPSIG	0x534D4150

struct smap {
    u_int64_t	base;
    u_int64_t	length;
    u_int32_t	type;
} __packed;

static struct smap smap;

void
bios_getmem(void)
{
    int64_t v;

    /* Parse system memory map */
    v86.ebx = 0;
    do {
#ifdef COMCONSOLE_DEBUG
	printf("GET SMAP %d: ", v86.ebx);
#endif
	v86.ctl = V86_FLAGS;
	v86.addr = 0x15;		/* int 0x15 function 0xe820*/
	v86.eax = 0xe820;
	v86.ecx = sizeof(struct smap);
	v86.edx = SMAPSIG;
	v86.es = VTOPSEG(&smap);
	v86.edi = VTOPOFF(&smap);
	v86int();
#ifdef COMCONSOLE_DEBUG
	printf("RESULT: EFL=%04x EAX=%08x SMAPTYPE %d BASE %08x%08x LEN %08x%08x\n",
		v86.efl, v86.eax, smap.type,
		(int)(smap.base >> 32), (int)smap.base,
		(int)(smap.length >> 32), (int)smap.length);
#endif
	if ((v86.efl & 1) || (v86.eax != SMAPSIG))
	    break;
	/* look for a low-memory segment that's large enough */
	if ((smap.type == 1) && (smap.base == 0) && (smap.length >= (512 * 1024))) {
	    bios_basemem = smap.length;
	    bios_howmem = 1;
	}
	/* look for the first segment in 'extended' memory */
	if ((smap.type == 1) && (smap.base == 0x100000)) {
	    bios_extmem = smap.length;
	}
    } while (v86.ebx != 0);

    /* Fall back to the old compatibility function for base memory */
    if (bios_basemem == 0) {
	v86.ctl = 0;
	v86.addr = 0x12;		/* int 0x12 */
	v86int();
	
	bios_basemem = (v86.eax & 0xffff) * 1024;
	bios_howmem = 2;
    }

    /* Fall back through several compatibility functions for extended memory */
    if (bios_extmem == 0) {
	v86.ctl = V86_FLAGS;
	v86.addr = 0x15;		/* int 0x15 function 0xe801*/
	v86.eax = 0xe801;
	v86int();
	if (!(v86.efl & 1)) {
	    v = ((v86.ecx & 0xffff) +
		((int64_t)(v86.edx & 0xffff) * 64)) * 1024;
	    if (v > 0x40000000)
		v = 0x40000000;
	    bios_extmem = v;
	}
    }
    if (bios_extmem == 0) {
	v86.ctl = 0;
	v86.addr = 0x15;		/* int 0x15 function 0x88*/
	v86.eax = 0x8800;
	v86int();
	bios_extmem = (v86.eax & 0xffff) * 1024;
    }

    /*
     * Set memtop to actual top of memory
     *
     * This is broken because the boot loader generally needs more than 16MB
     * now but the extmem usually calculates to ~14-16MB (which is the fully
     * segmented limit).  Disk I/O will use bounce buffers.
     *
     * Hack it for now.
     */
    memtop = 0x100000 + bios_extmem;	/* XXX ignored */
    memtop = 64 * 1024 * 1024;
}    
