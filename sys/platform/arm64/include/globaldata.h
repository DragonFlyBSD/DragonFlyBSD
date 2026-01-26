/*-
 * Copyright (c) Peter Wemm <peter@netplex.com.au>
 * Copyright (c) 2026 The DragonFly Project.
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
 */

#ifndef _MACHINE_GLOBALDATA_H_
#define _MACHINE_GLOBALDATA_H_

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

#ifndef _SYS_GLOBALDATA_H_
#include <sys/globaldata.h>
#endif
#ifndef _SYS_THREAD_H_
#include <sys/thread.h>
#endif
#ifndef _MACHINE_PARAM_H_
#include <machine/param.h>
#endif

/*
 * Minimal privatespace for arm64 - just an idle stack.
 * Unlike x86_64, we don't need TSS, trampoline frames, etc.
 */
struct arm64_privatespace {
	char		idlestack[UPAGES * PAGE_SIZE];
};

struct mdglobaldata {
	struct globaldata mi;
	struct arm64_privatespace *gd_prvspace;	/* per-cpu private data */
};

#define MDGLOBALDATA_BASEALLOC_SIZE	\
	((sizeof(struct mdglobaldata) + PAGE_MASK) & ~PAGE_MASK)
#define MDGLOBALDATA_BASEALLOC_PAGES	\
	(MDGLOBALDATA_BASEALLOC_SIZE / PAGE_SIZE)
#define MDGLOBALDATA_PAD		\
	(MDGLOBALDATA_BASEALLOC_SIZE - sizeof(struct mdglobaldata))

#define mdcpu	((struct mdglobaldata *)_get_mycpu())

#endif

#endif /* _MACHINE_GLOBALDATA_H_ */
