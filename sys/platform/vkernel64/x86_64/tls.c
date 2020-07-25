/*
 * Copyright (c) 2003,2004,2008 The DragonFly Project.  All rights reserved.
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysmsg.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/sysent.h>
#include <sys/sysctl.h>
#include <sys/tls.h>
#include <sys/reg.h>
#include <sys/globaldata.h>

#include <machine/cpu.h>
#include <machine/clock.h>
#include <machine/specialreg.h>
#include <machine/segments.h>
#include <machine/md_var.h>
#include <machine/pcb_ext.h>
#include <machine/globaldata.h>		/* CPU_prvspace */
#include <machine/smp.h>
#include <machine/pcb.h>

/*
 * set a TLS descriptor.  For x86_64 descriptor 0 identifies %fs and
 * descriptor 1 identifies %gs, and 0 is returned in sysmsg_result.
 *
 * Returns the value userland needs to load into %gs representing the
 * TLS descriptor or -1 on error.
 *
 * (int which, struct tls_info *info, size_t infosize)
 */
int
sys_set_tls_area(struct sysmsg *sysmsg, const struct set_tls_area_args *uap)
{
	struct tls_info info;
	int error;
	int i;

	/*
	 * Sanity checks
	 *
	 * which 0 == %fs, which 1 == %gs
	 */
	i = uap->which;
	if (i < 0 || i > 1)
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

	/*
	 * For x86_64 we can only adjust FSBASE and GSBASE
	 */
	curthread->td_tls.info[i] = info;
	set_user_TLS();
	sysmsg->sysmsg_result = 0;	/* segment descriptor $0 */
	return(0);
}

/*
 * Return the specified TLS descriptor to userland.
 *
 * Returns the value userland needs to load into %gs representing the
 * TLS descriptor or -1 on error.
 *
 * (int which, struct tls_info *info, size_t infosize)
 */
int
sys_get_tls_area(struct sysmsg *sysmsg, const struct get_tls_area_args *uap)
{
	struct tls_info info;
	int error;
	int i;

	/*
	 * Sanity checks
	 */
	i = uap->which;
	if (i < 0 || i > 1)
		return (ERANGE);
	if (uap->infosize < 0)
		return (EINVAL);

	info = curthread->td_tls.info[i];

	error = copyout(&info, uap->info, min(sizeof(info), uap->infosize));
	return(error);
}

/*
 * This function is a NOP because the TLS segments are proactively copied
 * by vmspace_ctl() when we switch to the (emulated) user process.
 */
void
set_user_TLS(void)
{
}
