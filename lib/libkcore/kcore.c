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
 * $DragonFly: src/lib/libkcore/kcore.c,v 1.6 2005/02/03 17:28:40 joerg Exp $
 */

#include <sys/kinfo.h>
#include <sys/param.h>
#include <sys/fcntl.h>

#include <err.h>
#include <errno.h>
#include <kcore.h>
#include <kvm.h>
#include <stdlib.h>

#include "kcore_private.h"

struct kcore_data kcore_global;

static int
kcore_open_int(struct kcore_data *kc, const char *execfile,
	       const char *corefile, char *errbuf)
{
	kc->kd = kvm_openfiles(execfile, corefile, NULL, O_RDONLY, errbuf);

	if (kc->kd == NULL)
		return(-1);
	else
		return(0);
}

int
kcore_wrapper_open(const char *execfile, const char *corefile, char *errbuf)
{
	return(kcore_open_int(&kcore_global, execfile, corefile, errbuf));
}

struct kcore_data *
kcore_open(const char *execfile, const char *corefile, char *errbuf)
{
	struct kcore_data *kc;

	kc = malloc(sizeof(*kc));
	if (kc == NULL)
		return(NULL);
	if (kcore_open_int(kc, execfile, corefile, errbuf)) {
		free(kc);
		return(NULL);
	}

	return(kc);
}

int
kcore_close(struct kcore_data *kc)
{
	int retval;

	if (kc == NULL) {
		retval = kvm_close(kcore_global.kd);
		return(retval);
	}

	retval = kvm_close(kc->kd);
	free(kc);
	return(retval);
}

int
kcore_get_generic(struct kcore_data *kc, struct nlist *nl,
		  void *data, size_t len)
{
	if (kc == NULL)
		kc = &kcore_global;

	if (nl[0].n_value == 0) {
		if ((kvm_nlist(kc->kd, nl) < 0) || (nl[0].n_value == 0)) {
			errno = EOPNOTSUPP;
			return(-1);
		}
	}
	if (kvm_read(kc->kd, nl[0].n_value, data, len) != (int)len) {
		warnx("cannot read %s: %s", nl[0].n_name, kvm_geterr(kc->kd));
		errno = EINVAL;
		return(-1);
	}
	return(0);
}
