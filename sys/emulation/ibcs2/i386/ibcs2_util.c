/*
 * Copyright (c) 1994 Christos Zoulas
 * Copyright (c) 1995 Frank van der Linden
 * Copyright (c) 1995 Scott Bartram
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *	from: svr4_util.c,v 1.5 1995/01/22 23:44:50 christos Exp
 * $FreeBSD: src/sys/i386/ibcs2/ibcs2_util.c,v 1.7 1999/12/15 23:01:45 eivind Exp $
 * $DragonFly: src/sys/emulation/ibcs2/i386/Attic/ibcs2_util.c,v 1.9 2004/11/12 00:09:16 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/nlookup.h>
#include <sys/malloc.h>
#include <sys/vnode.h>

#include "ibcs2_util.h"

#include <vm/vm_zone.h>

const char      ibcs2_emul_path[] = "/compat/ibcs2";

/*
 * Search an alternate path before passing pathname arguments on
 * to system calls. Useful for keeping a separate 'emulation tree'.
 *
 * If cflag is set, we check if an attempt can be made to create
 * the named file, i.e. we check if the directory it should
 * be in exists.
 */
int
ibcs2_emul_find(sgp, prefix, path, pbuf, cflag)
	caddr_t		 *sgp;		/* Pointer to stackgap memory */
	const char	 *prefix;
	char		 *path;
	char		**pbuf;
	int		  cflag;
{
	struct nlookupdata	 nd;
	struct nlookupdata	 ndroot;
	struct vattr		 vat;
	struct vattr		 vatroot;
	struct vnode		*vp;
	int			 error;
	char			*ptr, *buf, *cp;
	size_t			 sz, len;

	buf = (char *) malloc(MAXPATHLEN, M_TEMP, M_WAITOK);
	*pbuf = path;

	for (ptr = buf; (*ptr = *prefix) != '\0'; ptr++, prefix++)
		continue;

	sz = MAXPATHLEN - (ptr - buf);

	/* 
	 * If sgp is not given then the path is already in kernel space
	 */
	if (sgp == NULL)
		error = copystr(path, ptr, sz, &len);
	else
		error = copyinstr(path, ptr, sz, &len);

	if (error) {
		free(buf, M_TEMP);
		return error;
	}

	if (*ptr != '/') {
		free(buf, M_TEMP);
		return EINVAL;
	}

	/*
	 * We know that there is a / somewhere in this pathname.
	 * Search backwards for it, to find the file's parent dir
	 * to see if it exists in the alternate tree. If it does,
	 * and we want to create a file (cflag is set). We don't
	 * need to worry about the root comparison in this case.
	 */

	if (cflag) {
		for (cp = &ptr[len] - 1; *cp != '/'; cp--);
		*cp = '\0';

		error = nlookup_init(&nd, buf, UIO_SYSSPACE, NLC_FOLLOW);
		if (error == 0)
			error = nlookup(&nd);
		if (error) {
			nlookup_done(&nd);
			return (error);
		}
		*cp = '/';
	} else {

		error = nlookup_init(&nd, buf, UIO_SYSSPACE, NLC_FOLLOW);
		if (error == 0)
			error = nlookup(&nd);
		if (error) {
			nlookup_done(&nd);
			return (error);
		}

		/*
		 * We now compare the vnode of the ibcs2_root to the one
		 * vnode asked. If they resolve to be the same, then we
		 * ignore the match so that the real root gets used.
		 * This avoids the problem of traversing "../.." to find the
		 * root directory and never finding it, because "/" resolves
		 * to the emulation root directory. This is expensive :-(
		 */
		error = nlookup_init(&ndroot, ibcs2_emul_path, UIO_SYSSPACE,
					NLC_FOLLOW);
		if (error == 0)
			error = nlookup(&ndroot);
		if (error == 0)
			error = cache_vref(nd.nl_ncp, nd.nl_cred, &vp);
		if (error) 
			goto done;
		error = VOP_GETATTR(vp, &vat, nd.nl_td);
		vrele(vp);
		if (error == 0)
			error = cache_vref(ndroot.nl_ncp, nd.nl_cred, &vp);
		if (error)
			goto done;
		error = VOP_GETATTR(vp, &vatroot, nd.nl_td);
		vrele(vp);
		if (error)
			goto done;

		if (vat.va_fsid == vatroot.va_fsid &&
		    vat.va_fileid == vatroot.va_fileid) {
			error = ENOENT;
			goto done;
		}

	}
	if (sgp == NULL) {
		*pbuf = buf;
	} else {
		sz = &ptr[len] - buf;
		*pbuf = stackgap_alloc(sgp, sz + 1);
		error = copyout(buf, *pbuf, sz);
		free(buf, M_TEMP);
	}
done:
	nlookup_done(&nd);
	if (!cflag)
		nlookup_done(&ndroot);
	return (error);
}
