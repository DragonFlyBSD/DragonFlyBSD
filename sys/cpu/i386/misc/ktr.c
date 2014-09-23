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
 */
/*
 * Kernel tracepoint facility.
 */

#include "opt_ktr.h"

#include <sys/param.h>
#include <sys/cons.h>
#include <sys/kernel.h>
#include <sys/libkern.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/ktr.h>

/*
 * This routine fills in the ktr_caller1 and ktr_caller2 fields by
 * tracing back through the kernel stack to locate the stack frames
 * and return addresses.
 *
 *
 *	[first argument]
 *	[retpc]
 *	[frameptr]		-> points to caller's frame pointer
 * sp ->[junk]
 */

static __inline 
void **
FRAMEUP(void **frameptr)
{
    void **newframeptr;

    newframeptr = (void **)frameptr[0];
    if (((uintptr_t)newframeptr ^ (uintptr_t)frameptr) & ~16383)
	newframeptr = frameptr;
    return(newframeptr);
}

void
cpu_ktr_caller(struct ktr_entry *_ktr)
{
    struct ktr_entry *ktr;
    void **frameptr;

    frameptr = (void **)&_ktr - 2;	/* frame, retpc to ktr_log */
    ktr = _ktr;
    frameptr = FRAMEUP(frameptr);	/* frame, retpc to traced function */
    frameptr = FRAMEUP(frameptr);	/* frame, caller1 of traced function */
    ktr->ktr_caller1 = frameptr[1];
    frameptr = FRAMEUP(frameptr);	/* frame, caller2 of caller1 */
    ktr->ktr_caller2 = frameptr[1];
}
