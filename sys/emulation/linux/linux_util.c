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
 * $FreeBSD: src/sys/compat/linux/linux_util.c,v 1.12.2.2 2001/11/05 19:08:23 marcel Exp $
 * $DragonFly: src/sys/emulation/linux/linux_util.c,v 1.10 2004/11/12 00:09:18 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/nlookup.h>
#include <sys/malloc.h>
#include <sys/vnode.h>

#include "linux_util.h"

const char      linux_emul_path[] = "/compat/linux";

/*
 * Search for an alternate path before passing pathname arguments on
 * to system calls.
 *
 * Only signal an error if something really bad happens.  In most cases
 * we can just return the untranslated path, eg. name lookup failures.
 */
int
linux_copyin_path(char *uname, char **kname, int flags)
{
	struct thread *td = curthread;
	struct nlookupdata nd, ndroot;
	struct vattr vat, vatroot;
	struct vnode *vp, *vproot;
	char *buf, *cp;
	int error, length, dummy;

	buf = (char *) malloc(MAXPATHLEN, M_TEMP, M_WAITOK);
	*kname = buf;

	/*
	 * Don't bother trying to translate if the path is relative.
	 */
	if (fubyte(uname) != '/')
		goto dont_translate;

	/*
	 * The path is absolute.  Prepend the buffer with the emulation
	 * path and copy in.
	 */
	length = strlen(linux_emul_path);
	bcopy(linux_emul_path, buf, length);
	error = copyinstr(uname, buf + length, MAXPATHLEN - length, &dummy);
	if (error)
		goto done;

	switch (flags) {
	case LINUX_PATH_CREATE:
		/*
		 * Check to see if the parent directory exists in the
		 * emulation tree.  Walk the string backwards to find
		 * the last '/'.
		 */
		cp = buf + strlen(buf);
		while (--cp >= buf) {
			if (*cp == '/')
				break;
		}
		if (cp < buf)
			goto dont_translate;
		*cp = 0;

		error = nlookup_init(&nd, buf, UIO_SYSSPACE, NLC_FOLLOW);
		if (error == 0)
			error = nlookup(&nd);
		nlookup_done(&nd);
		if (error)
			goto dont_translate;
		*cp = '/';
		return (0);
	case LINUX_PATH_EXISTS:
		error = nlookup_init(&nd, buf, UIO_SYSSPACE, NLC_FOLLOW);
		if (error == 0)
			error = nlookup(&nd);
		vp = NULL;
		if (error == 0)
			error = cache_vref(nd.nl_ncp, nd.nl_cred, &vp);
		nlookup_done(&nd);
		if (error)
			goto dont_translate;

		/*
		 * We now compare the vnode of the linux_root to the one
		 * vnode asked. If they resolve to be the same, then we
		 * ignore the match so that the real root gets used.
		 * This avoids the problem of traversing "../.." to find the
		 * root directory and never finding it, because "/" resolves
		 * to the emulation root directory. This is expensive :-(
		 *
		 * The next three function calls should not return errors.
		 * If they do something is seriously wrong, eg. the
		 * emulation subtree does not exist.  Cross our fingers
		 * and return the untranslated path if something happens.
		 */
		error = nlookup_init(&ndroot, linux_emul_path, UIO_SYSSPACE,
					NLC_FOLLOW);
		if (error == 0)
			error = nlookup(&ndroot);
		vproot = NULL;
		if (error == 0) {
			error = cache_vref(ndroot.nl_ncp, ndroot.nl_cred,
						&vproot);
		}
		nlookup_done(&ndroot);
		if (error) {
			vrele(vp);
			goto dont_translate;
		}
		
		error = VOP_GETATTR(vp, &vat, td);
		if (error == 0) {
			error = VOP_GETATTR(vproot, &vatroot, td);
			if (error == 0) {
				if (vat.va_fsid == vatroot.va_fsid &&
				    vat.va_fileid == vatroot.va_fileid)
					error = ENOENT;
			}
		}
		vrele(vp);
		vrele(vproot);
		if (error)
			goto dont_translate;
		return (0);
	default:
		error = EINVAL;
		goto done;
	}
	
dont_translate:
	error = copyinstr(uname, buf, MAXPATHLEN, &dummy);
done:
	if (error)
		linux_free_path(kname);
	return (error);
}

/*
 * Smaller version of the above for translating in kernel buffers.  Only
 * used in exec_linux_imgact_try().  Only check is path exists.
 */
int
linux_translate_path(char *path, int size)
{
	struct nlookupdata nd;
	char *buf;
	int error, length, dummy;

	buf = (char *) malloc(MAXPATHLEN, M_TEMP, M_WAITOK);
	length = strlen(linux_emul_path);
	bcopy(linux_emul_path, buf, length);
	error = copystr(path, buf + length, MAXPATHLEN - length, &dummy);
	if (error)
		goto cleanup;
	
	/*
	 * If this errors, then the path probably doesn't exist.
	 */
	error = nlookup_init(&nd, buf, UIO_SYSSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	nlookup_done(&nd);
	if (error) {
		error = 0;
		goto cleanup;
	}

	/*
	 * The alternate path does exist.  Return it in the buffer if
	 * it fits.
	 */
	if (strlen(buf) + 1 <= size)
		error = copystr(buf, path, size, &dummy);

cleanup:

	free(buf, M_TEMP);
	return (error);
}
