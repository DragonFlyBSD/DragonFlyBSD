/*
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
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
 * 
 * $DragonFly: src/sys/kern/vfs_nlookup.c,v 1.1 2004/09/28 00:25:29 dillon Exp $
 */
/*
 * nlookup() is the 'new' namei interface.  Rather then return directory and
 * leaf vnodes (in various lock states) the new interface instead deals in
 * namecache records.  Namecache records may represent both a positive or
 * a negative hit.  The namespace is locked via the namecache record instead
 * of via the vnode, and only the leaf namecache record (representing the
 * filename) needs to be locked.
 *
 * This greatly improves filesystem parallelism and is a huge simplification
 * of the API verses the old vnode locking / namei scheme.
 *
 * Filesystems must actively control the caching aspects of the namecache,
 * and since namecache pointers are used as handles they are non-optional
 * even for filesystems which do not generally wish to cache things.  It is
 * intended that a separate cache coherency API will be constructed to handle
 * these issues.
 */

#include "opt_ktrace.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/filedesc.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/nlookup.h>
#include <sys/malloc.h>
#include <vm/vm_zone.h>

#ifdef KTRACE
#include <sys/ktrace.h>
#endif

/*
 * Initialize a nlookup() structure, early error return for copyin faults
 * or a degenerate empty string (which is not allowed).
 */
int
nlookup_init(struct nlookupdata *nd, const char *path, enum uio_seg seg, 
		int flags)
{
    size_t pathlen;
    struct proc *p;
    thread_t td;
    int error;

    td = curthread;
    p = td->td_proc;

    /*
     * note: the pathlen set by copy*str() includes the terminating \0.
     */
    bzero(nd, sizeof(struct nlookupdata));
    nd->nl_path = zalloc(namei_zone);
    nd->nl_flags |= NLC_HASBUF;
    if (seg == UIO_SYSSPACE) 
	error = copystr(path, nd->nl_path, MAXPATHLEN, &pathlen);
    else
	error = copyinstr(path, nd->nl_path, MAXPATHLEN, &pathlen);

    /*
     * Don't allow empty pathnames.
     * POSIX.1 requirement: "" is not a vaild file name.
     */
    if (error == 0 && pathlen <= 1)
	error = ENOENT;

    if (error == 0) {
	if (p->p_fd) {
	    nd->nl_ncp = cache_hold(p->p_fd->fd_ncdir);
	    nd->nl_rootncp = cache_hold(p->p_fd->fd_nrdir);
	    if (p->p_fd->fd_njdir)
		nd->nl_jailncp = cache_hold(p->p_fd->fd_njdir);
	} else {
	    nd->nl_ncp = cache_vptoncp(rootvnode);
	    nd->nl_rootncp = cache_hold(nd->nl_ncp);
	    nd->nl_jailncp = cache_hold(nd->nl_ncp);
	}
	nd->nl_td = td;
	nd->nl_cred = crhold(p->p_ucred);
	nd->nl_flags |= flags;
    } else {
	nlookup_done(nd);
    }
    return(error);
}

/*
 * Cleanup a nlookupdata structure after we are through with it.
 */
void
nlookup_done(struct nlookupdata *nd)
{
    if (nd->nl_ncp) {
	cache_drop(nd->nl_ncp);
	nd->nl_ncp = NULL;
    }
    if (nd->nl_rootncp) {
	cache_drop(nd->nl_rootncp);
	nd->nl_rootncp = NULL;
    }
    if (nd->nl_jailncp) {
	cache_drop(nd->nl_jailncp);
	nd->nl_jailncp = NULL;
    }
    if ((nd->nl_flags & NLC_HASBUF) && nd->nl_path) {
	zfree(namei_zone, nd->nl_path);
	nd->nl_path = NULL;
    }
    if (nd->nl_cred) {
	crfree(nd->nl_cred);
	nd->nl_cred = NULL;
    }
}

/*
 * Simple all-in-one nlookup
 */
struct namecache *
nlookup_simple(const char *str, enum uio_seg seg, int niflags, int *error)
{
    struct nlookupdata nd;
    struct namecache *ncp;

    *error = nlookup_init(&nd, str, seg, niflags);
    if (*error == 0) {
	    *error = nlookup(&nd);
	    ncp = nd.nl_ncp;	/* keep hold ref from structure */
	    nd.nl_ncp = NULL;	/* and NULL out */
	    nlookup_done(&nd);
    } else {
	    ncp = NULL;
    }
    return(ncp);
}

/*
 * Do a generic nlookup.  Note that the passed nd is not nlookup_done()'d
 * on return, even if an error occurs.  If no error occurs the returned
 * nl_ncp is always referenced and locked, otherwise it may or may not be.
 */
int
nlookup(struct nlookupdata *nd)
{
    struct nlcomponent nlc;
    struct namecache *ncp;
    char *ptr;
    int error;
    int len;

#ifdef KTRACE
    if (KTRPOINT(nd->nl_td, KTR_NAMEI))
	ktrnamei(nd->nl_td->td_proc->p_tracep, nd->nl_path);
#endif
    bzero(&nlc, sizeof(nlc));

    /*
     * Setup for the loop.  The current working namecache element must
     * be in an unlocked state.  This typically the case on entry except
     * when stringing nlookup()'s along in a chain, since nlookup(0 always
     * returns nl_ncp in a locked state.
     */
    nd->nl_loopcnt = 0;
    if (nd->nl_flags & NLC_NCPISLOCKED) {
	nd->nl_flags &= ~NLC_NCPISLOCKED;
	cache_unlock(nd->nl_ncp);
    }
    ptr = nd->nl_path;

    /*
     * Loop on the path compoenents
     */
    for (;;) {
	/*
	 * Check if the root directory should replace the current
	 * directory.  This is done at the start of a translation
	 * or after a symbolic link has been found.  In other cases
	 * ptr will never be pointing at a '/'.
	 */
	if (*ptr == '/') {
	    do {
		++ptr;
	    } while (*ptr == '/');
	    cache_drop(nd->nl_ncp);
	    nd->nl_ncp = cache_hold(nd->nl_rootncp);
	    continue;
	}

	/*
	 * Extract the path component
	 */
	nlc.nlc_nameptr = ptr;
	while (*ptr && *ptr != '/')
	    ++ptr;
	nlc.nlc_namelen = ptr - nlc.nlc_nameptr;

	/*
	 * Resolve the namespace.  The ncp returned by cache_nlookup()
	 * is referenced and locked.
	 */
	ncp = cache_nlookup(nd->nl_ncp, &nlc);
	if (ncp->nc_flag & NCF_UNRESOLVED) {
	    error = cache_resolve(ncp);
	} else {
	    error = ncp->nc_error;
	}
	if (error) {
	    cache_put(ncp);
	    break;
	}

	/*
	 * If the element is a symlink and it is either not the last
	 * element or it is the last element and we are allowed to
	 * follow symlinks, resolve the symlink.
	 */
	if ((ncp->nc_flag & NCF_ISSYMLINK) &&
	    (*ptr || (nd->nl_flags & NLC_FOLLOW))
	) {
	    if (nd->nl_loopcnt++ >= MAXSYMLINKS) {
		    error = ELOOP;
		    cache_put(ncp);
		    break;
	    }
	    error = nreadsymlink(nd, ncp, &nlc);
	    if (error) {
		cache_put(ncp);
		break;
	    }

	    /*
	     * Concatenate trailing path elements onto the returned symlink.
	     * Note that if the path component (ptr) is not exhausted, it
	     * will being with a '/', so we do not have to add another one.
	     *
	     * The symlink may not be empty.
	     */
	    len = strlen(ptr);
	    if (nlc.nlc_namelen == 0 || nlc.nlc_namelen + len >= MAXPATHLEN) {
		error = nlc.nlc_namelen ? ENAMETOOLONG : ENOENT;
		zfree(namei_zone, nlc.nlc_nameptr);
		cache_put(ncp);
		break;
	    }
	    bcopy(ptr, nlc.nlc_nameptr + nlc.nlc_namelen, len + 1);
	    if (nd->nl_flags & NLC_HASBUF)
		zfree(namei_zone, nd->nl_path);
	    nd->nl_path = nlc.nlc_nameptr;
	    nd->nl_flags |= NLC_HASBUF;
	    ptr = nd->nl_path;

	    /*
	     * Go back up to the top to resolve any initial '/'s in the
	     * symlink.
	     */
	    continue;
	}

	/*
	 * Skip any slashes to get to the next element.  If there 
	 * are any slashes at all the current element must be a
	 * directory.  If it isn't we break without incrementing
	 * ptr and fall through to the failure case below.
	 */
	while (*ptr == '/') {
	    if ((ncp->nc_flag & NCF_ISDIR) == 0)
		break;
	    ++ptr;
	}

	/*
	 * Continuation case: additional elements and the current
	 * element is a directory.
	 */
	if (*ptr && (ncp->nc_flag & NCF_ISDIR)) {
	    cache_drop(nd->nl_ncp);
	    cache_unlock(ncp);
	    nd->nl_ncp = ncp;
	    continue;
	}

	/*
	 * Failure case: additional elements and the current element
	 * is not a directory
	 */
	if (*ptr) {
	    cache_put(ncp);
	    error = ENOTDIR;
	    break;
	}

	/*
	 * XXX vnode canvmio (test in mmap(), read(), and write())
	 */

	/*
	 * Termination: no more elements.
	 */
	cache_drop(nd->nl_ncp);
	nd->nl_ncp = ncp;
	nd->nl_flags |= NLC_NCPISLOCKED;
	error = 0;
	break;
    }
    return(error);
}

/*
 * Read the contents of a symlink, allocate a path buffer out of the
 * namei_zone and initialize the supplied nlcomponent with the result.
 *
 * If an error occurs no buffer will be allocated or returned in the nlc.
 */
int
nreadsymlink(struct nlookupdata *nd, struct namecache *ncp, 
		struct nlcomponent *nlc)
{
    struct iovec aiov;
    struct uio auio;
    int linklen;
    int error;
    char *cp;

    nlc->nlc_nameptr = NULL;
    nlc->nlc_namelen = 0;
    if (ncp->nc_vp == NULL)
	return(ENOENT);
    if ((error = vget(ncp->nc_vp, NULL, LK_SHARED, nd->nl_td)) != 0)
	return(error);
    cp = zalloc(namei_zone);
    aiov.iov_base = cp;
    aiov.iov_len = MAXPATHLEN;
    auio.uio_iov = &aiov;
    auio.uio_iovcnt = 1;
    auio.uio_offset = 0;
    auio.uio_rw = UIO_READ;
    auio.uio_segflg = UIO_SYSSPACE;
    auio.uio_td = nd->nl_td;
    auio.uio_resid = MAXPATHLEN - 1;
    error = VOP_READLINK(ncp->nc_vp, &auio, nd->nl_cred);
    if (error)
	goto fail;
    linklen = MAXPATHLEN - auio.uio_resid;
    if (varsym_enable) {
	linklen = varsymreplace(cp, linklen, MAXPATHLEN - 1);
	if (linklen < 0) {
	    error = ENAMETOOLONG;
	    goto fail;
	}
    }
    cp[linklen] = 0;
    nlc->nlc_nameptr = cp;
    nlc->nlc_namelen = linklen;
    vput(ncp->nc_vp);
    return(0);
fail:
    zfree(namei_zone, cp);
    vput(ncp->nc_vp);
    return(error);
}

