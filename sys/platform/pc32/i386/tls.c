/*
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by David Xu <davidxu@t2t2.com> and Matthew Dillon <dillon@backplane.com>
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
 * $DragonFly: src/sys/platform/pc32/i386/tls.c,v 1.9 2008/06/29 19:04:01 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/sysent.h>
#include <sys/sysctl.h>
#include <sys/tls.h>
#include <sys/reg.h>
#include <sys/thread2.h>

#include <machine/cpu.h>
#include <machine/clock.h>
#include <machine/specialreg.h>
#include <machine/bootinfo.h>
#include <machine/md_var.h>
#include <machine/pcb_ext.h>		/* pcb.h included via sys/user.h */
#include <machine/globaldata.h>		/* CPU_prvspace */
#include <machine/smp.h>

/*
 * set a TLS descriptor and resync the GDT.  A descriptor may be cleared
 * by passing info=NULL and infosize=0.  Note that hardware limitations may
 * cause the size passed in tls_info to be approximated. 
 *
 * Returns the value userland needs to load into %gs representing the 
 * TLS descriptor or -1 on error.
 *
 * (int which, struct tls_info *info, size_t infosize)
 *
 * MPSAFE
 */
int
sys_set_tls_area(struct set_tls_area_args *uap)
{
	struct tls_info info;
	struct segment_descriptor *desc;
	int error;
	int i;

	/*
	 * Sanity checks
	 */
	i = uap->which;
	if (i < 0 || i >= NGTLS)
		return (ERANGE);
	if (uap->infosize < 0)
		return (EINVAL);

	/*
	 * Maintain forwards compatibility with future extensions.
	 */
	if (uap->infosize != sizeof(info)) {
		bzero(&info, sizeof(info));
		error = copyin(uap->info, &info, 
				min(sizeof(info), uap->infosize));
	} else {
		error = copyin(uap->info, &info, sizeof(info));
	}
	if (error)
		return (error);
	if (info.size < -1)
		return (EINVAL);
	if (info.size > (1 << 20))
		info.size = (info.size + PAGE_MASK) & ~PAGE_MASK;

	/*
	 * Load the descriptor.  A critical section is required in case
	 * an interrupt thread comes along and switches us out and then back
	 * in.
	 */
	desc = &curthread->td_tls.tls[i];
	crit_enter();
	if (info.size == 0) {
		bzero(desc, sizeof(*desc));
	} else {
		desc->sd_lobase = (intptr_t)info.base;
		desc->sd_hibase = (intptr_t)info.base >> 24;
		desc->sd_def32 = 1;
		desc->sd_type = SDT_MEMRWA;
		desc->sd_dpl = SEL_UPL;
		desc->sd_xx = 0;
		desc->sd_p = 1;
		if (info.size == -1) {
			/*
			 * A descriptor size of -1 is a hack to map the
			 * whole address space.  This type of mapping is
			 * required for direct-tls accesses of variable
			 * data, e.g. %gs:OFFSET where OFFSET is negative.
			 */
			desc->sd_lolimit = -1;
			desc->sd_hilimit = -1;
			desc->sd_gran = 1;
		} else if (info.size >= (1 << 20)) {
			/*
			 * A descriptor size greater then 1MB requires page
			 * granularity (the lo+hilimit field is only 20 bits)
			 */
			desc->sd_lolimit = info.size >> PAGE_SHIFT;
			desc->sd_hilimit = info.size >> (PAGE_SHIFT + 16);
			desc->sd_gran = 1;
		} else {
			/*
			 * Otherwise a byte-granular size is supported.
			 */
			desc->sd_lolimit = info.size;
			desc->sd_hilimit = info.size >> 16;
			desc->sd_gran = 0;
		}
	}
	crit_exit();
	uap->sysmsg_result = GSEL(GTLS_START + i, SEL_UPL);
	set_user_TLS();
	return(0);
}
	
/*
 * Return the specified TLS descriptor to userland.
 *
 * Returns the value userland needs to load into %gs representing the 
 * TLS descriptor or -1 on error.
 *
 * (int which, struct tls_info *info, size_t infosize)
 *
 * MPSAFE
 */
int
sys_get_tls_area(struct get_tls_area_args *uap)
{
	struct tls_info info;
	struct segment_descriptor *desc;
	int error;
	int i;

	/*
	 * Sanity checks
	 */
	i = uap->which;
	if (i < 0 || i >= NGTLS)
		return (ERANGE);
	if (uap->infosize < 0)
		return (EINVAL);

	/*
	 * unpack the descriptor, ENOENT is returned for any descriptor
	 * which has not been loaded.  uap->info may be NULL.
	 */
	desc = &curthread->td_tls.tls[i];
	if (desc->sd_p) {
		if (uap->info && uap->infosize > 0) {
			bzero(&info, sizeof(info));
			info.base = (void *)(intptr_t)
				((desc->sd_hibase << 24) | desc->sd_lobase);
			info.size = (desc->sd_hilimit << 16) | desc->sd_lolimit;
			if (desc->sd_gran)
				info.size <<= PAGE_SHIFT;
			error = copyout(&info, uap->info,
					min(sizeof(info), uap->infosize));
		} else {
			error = 0;
		}
		uap->sysmsg_result = GSEL(GTLS_START + i, SEL_UPL);
	} else {
		error = ENOENT;
	}
	return(error);
}
