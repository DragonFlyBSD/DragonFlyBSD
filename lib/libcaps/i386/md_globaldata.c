/*
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com>
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
 * $DragonFly: src/lib/libcaps/i386/md_globaldata.c,v 1.2 2003/12/07 04:21:54 dillon Exp $
 */
#include "../defs.h"
#include <machine/segments.h>
#include <machine/sysarch.h>

int __mycpu__dummy;	/* for the MP lock functions */

void
md_gdinit1(globaldata_t gd)
{
    union descriptor desc;
    int error;

    bzero(&desc, sizeof(desc));
    desc.sd.sd_lolimit = sizeof(struct globaldata);
    desc.sd.sd_lobase = (uintptr_t)gd & 0xFFFFFF;
    desc.sd.sd_hibase = (uintptr_t)gd >> 24;
    desc.sd.sd_type = SDT_MEMRW;
    desc.sd.sd_dpl = SEL_UPL;
    desc.sd.sd_p = 1;
    desc.sd.sd_hilimit = 0;
    desc.sd.sd_xx = 0;
    desc.sd.sd_def32 = 1;
    desc.sd.sd_gran = 0;

    error = i386_set_ldt(gd->gd_cpuid, &desc, 1);
    if (error < 0)
	panic("i386_set_ldt cpu %d failed\n", gd->gd_cpuid);
}

void
md_gdinit2(globaldata_t gd)
{
    _set_mycpu(LSEL(gd->gd_cpuid, SEL_UPL));
}

