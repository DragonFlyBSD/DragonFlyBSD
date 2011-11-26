/*
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Joerg Sonnenberger <joerg@bec.de>.
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
 * $DragonFly: src/lib/libkcore/kcore_file.c,v 1.5 2007/04/29 01:36:03 dillon Exp $
 */

#define _KERNEL_STRUCTURES

#include <sys/user.h>	/* MUST BE FIRST */
#include <sys/param.h>
#include <sys/file.h>
#include <sys/kcore.h>

#include <err.h>
#include <errno.h>
#include <kcore.h>
#include <kvm.h>
#include <nlist.h>
#include <stdlib.h>

#include "kcore_private.h"

int
kcore_get_files(struct kcore_data *kc, struct kinfo_file **files, size_t *len)
{
	struct kinfo_proc *procs, *oprocs;
	struct proc p;
	struct filedesc fdp;
	struct file fp, *fpp;
	size_t len_procs;
	int maxfiles, n, retval;

	if (kc == NULL)
		kc = &kcore_global;

	if ((retval = kcore_get_procs(kc, &procs, &len_procs)) != 0)
		return(retval);
	if (len_procs == 0) { /* no procs, no files */
		*files = NULL;
		*len = 0;
		return(0);
	}

	if ((retval = kcore_get_maxfiles(kc, &maxfiles)) != 0) {
		free(procs);
		return(retval);
	}

	*files = malloc(maxfiles * sizeof(struct kinfo_file));
	if (*files == NULL) {
		free(procs);
		return(ENOMEM);
	}
	*len = 0;

	oprocs = procs;
	for (; len_procs-- > 0; procs++) {
		if (kvm_read(kc->kd, procs->kp_paddr, &p,
			     sizeof (p)) != sizeof(p)) {
			warnx("cannot read proc at %p for pid %d\n",
			      (void *)procs->kp_paddr, procs->kp_pid);
			continue;
		}
		if (p.p_fd == NULL || procs->kp_stat == SIDL)
			continue;
		if (kvm_read(kc->kd, (long)p.p_fd, &fdp,
			     sizeof (fdp)) != sizeof(fdp)) {
			warnx("cannot read filedesc at %p for pid %d\n",
			      p.p_fd, procs->kp_pid);
			continue;
		}
		for (n = 0; n < fdp.fd_nfiles; n++) {
			if (kvm_read(kc->kd, (long)(&fdp.fd_files[n].fp), &fpp,
				     sizeof(fpp)) != sizeof(fpp)) {
				warnx("cannot read filep  at %p for pid %d\n",
				      &fdp.fd_files[n].fp, procs->kp_pid);
			}
			if (fpp == NULL)
				continue;
			if (kvm_read(kc->kd, (long)fpp, &fp,
				     sizeof(fp)) != sizeof(fp)) {
				warnx("cannot read file at %p for pid %d\n",
				      fpp, procs->kp_pid);
				continue;
			}
			kcore_make_file(*files + *len, &fp, procs->kp_pid, 0, n);
			(*len)++;
		}
	}	

	*files = reallocf(*files, *len * sizeof(struct kinfo_file));
	if (*files == NULL)
		err(1, "realloc");
	free(oprocs);
	return(0);
}

int
kcore_get_maxfiles(struct kcore_data *kc, int *maxfiles)
{
	static struct nlist nl[] = {
		{ "_maxfiles", 0, 0, 0, 0},
		{ NULL, 0, 0, 0, 0}
	};

	return(kcore_get_generic(kc, nl, maxfiles, sizeof(*maxfiles)));
}

int
kcore_get_openfiles(struct kcore_data *kc, int *openfiles)
{
	static struct nlist nl[] = {
		{ "_nfiles", 0, 0, 0, 0},
		{ NULL, 0, 0, 0, 0}
	};

	return(kcore_get_generic(kc, nl, openfiles, sizeof(*openfiles)));
}
