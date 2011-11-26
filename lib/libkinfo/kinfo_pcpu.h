/*
 * Copyright (c) 2004, 2005 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Hiten Pandya <hmp@dragonflybsd.org>.
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
 * $DragonFly: src/lib/libkinfo/kinfo_pcpu.h,v 1.2 2005/04/27 16:16:30 hmp Exp $
 */

/*
 * Common code for managing per-cpu statistics counters.
 */

#ifndef _STRING_H_
#include <string.h>
#endif

#define	PCPU_STATISTICS_PROT(proto,type) \
void proto ##_pcpu_statistics(type *percpu, type *total, int ncpu)

#define PCPU_STATISTICS_FUNC(proto,type,fldtype)              \
void                                                          \
proto ##_pcpu_statistics(type *percpu, type *total, int ncpu) \
{                                                             \
    int cpu, off, siz;                                        \
    siz = sizeof(type);                                       \
                                                              \
    if (!percpu || !total)                                    \
        return;                                               \
                                                              \
    bzero(total, siz);                                        \
    if (ncpu == 1) {                                          \
        *total = percpu[0];                                   \
    } else {                                                  \
        for (cpu = 0; cpu < ncpu; ++cpu) {                    \
            for (off = 0; off < siz; off += sizeof(fldtype)) {\
                *(fldtype *)((char *)(*(&total)) + off) +=    \
                *(fldtype *)((char *)&percpu[cpu] + off);     \
            }                                                 \
        }                                                     \
    }                                                         \
}
