/*-
 * Copyright (c) Peter Wemm <peter@netplex.com.au> All rights reserved.
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.net> All rights reserved.
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
 * $FreeBSD: src/sys/i386/include/globaldata.h,v 1.11.2.1 2000/05/16 06:58:10 dillon Exp $
 * $DragonFly: src/sys/sys/globaldata.h,v 1.1 2003/06/28 04:16:05 dillon Exp $
 */

#ifndef _SYS_GLOBALDATA_H_
#define _SYS_GLOBALDATA_H_

/*
 * This structure maps out the global data that needs to be kept on a
 * per-cpu basis.  genassym uses this to generate offsets for the assembler
 * code.  The machine-dependant portions of this file can be found in
 * <machine/globaldata.h>, but only MD code should retrieve it.
 *
 * The SMP parts are setup in pmap.c and locore.s for the BSP, and
 * mp_machdep.c sets up the data for the AP's to "see" when they awake.
 * The reason for doing it via a struct is so that an array of pointers
 * to each CPU's data can be set up for things like "check curproc on all
 * other processors"
 *
 * NOTE! this structure needs to remain compatible between module accessors
 * and the kernel, so we can't throw in lots of #ifdef's.
 */

#ifndef _SYS_TIME_H_
#include <sys/time.h>	/* struct timeval */
#endif

struct privatespace;

struct globaldata {
	struct privatespace *gd_prvspace;	/* self-reference */
	struct thread	*gd_curthread;
	struct thread	*gd_idletd;		/* a ilttle messy but it works */
	int		gd_tdfreecount;		/* new thread cache */
	int		gd_reqpri;		/* highest pri blocked thread */
	TAILQ_HEAD(,thread) gd_tdfreeq;		/* new thread cache */
	TAILQ_HEAD(,thread) gd_tdrunq;		/* runnable threads */
	u_int		gd_cpuid;
	struct timeval	gd_stattv;
	int		gd_inside_intr;
	int		gd_astpending;		/* sorta MD but easier here */
	/* extended by <machine/pcpu.h> */
};

#endif
