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
 * $DragonFly: src/sys/kern/vfs_nlookup.c,v 1.20 2006/10/27 04:56:31 dillon Exp $
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
#include <sys/stat.h>
#include <sys/objcache.h>

#ifdef KTRACE
#include <sys/ktrace.h>
#endif

/*
 * Initialize a nlookup() structure, early error return for copyin faults
 * or a degenerate empty string (which is not allowed).
 *
 * The first process proc0's credentials are used if the calling thread
 * is not associated with a process context.
 */
int
nlookup_init(struct nlookupdata *nd, 
	     const char *path, enum uio_seg seg, int flags)
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
    nd->nl_path = objcache_get(namei_oc, M_WAITOK);
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
	if (p && p->p_fd) {
	    cache_copy(&p->p_fd->fd_ncdir, &nd->nl_nch);
	    cache_copy(&p->p_fd->fd_nrdir, &nd->nl_rootnch);
	    if (p->p_fd->fd_njdir.ncp)
		cache_copy(&p->p_fd->fd_njdir, &nd->nl_jailnch);
	    nd->nl_cred = crhold(p->p_ucred);
	} else {
	    cache_copy(&rootnch, &nd->nl_nch);
	    cache_copy(&nd->nl_nch, &nd->nl_rootnch);
	    cache_copy(&nd->nl_nch, &nd->nl_jailnch);
	    nd->nl_cred = crhold(proc0.p_ucred);
	}
	nd->nl_td = td;
	nd->nl_flags |= flags;
    } else {
	nlookup_done(nd);
    }
    return(error);
}

/*
 * This works similarly to nlookup_init() but does not assume a process
 * context.  rootnch is always chosen for the root directory and the cred
 * and starting directory are supplied in arguments.
 */
int
nlookup_init_raw(struct nlookupdata *nd, 
	     const char *path, enum uio_seg seg, int flags,
	     struct ucred *cred, struct nchandle *ncstart)
{
    size_t pathlen;
    thread_t td;
    int error;

    td = curthread;

    bzero(nd, sizeof(struct nlookupdata));
    nd->nl_path = objcache_get(namei_oc, M_WAITOK);
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
	cache_copy(ncstart, &nd->nl_nch);
	cache_copy(&rootnch, &nd->nl_rootnch);
	cache_copy(&rootnch, &nd->nl_jailnch);
	nd->nl_cred = crhold(cred);
	nd->nl_td = td;
	nd->nl_flags |= flags;
    } else {
	nlookup_done(nd);
    }
    return(error);
}

/*
 * Set a different credential; this credential will be used by future
 * operations performed on nd.nl_open_vp and nlookupdata structure.
 */
void
nlookup_set_cred(struct nlookupdata *nd, struct ucred *cred)
{
	KKASSERT(nd->nl_cred != NULL);

	if (nd->nl_cred != cred) {
		cred = crhold(cred);
		crfree(nd->nl_cred);
		nd->nl_cred = cred;
	}
}

/*
 * Cleanup a nlookupdata structure after we are through with it.  This may
 * be called on any nlookupdata structure initialized with nlookup_init().
 * Calling nlookup_done() is mandatory in all cases except where nlookup_init()
 * returns an error, even if as a consumer you believe you have taken all
 * dynamic elements out of the nlookupdata structure.
 */
void
nlookup_done(struct nlookupdata *nd)
{
    if (nd->nl_nch.ncp) {
	if (nd->nl_flags & NLC_NCPISLOCKED) {
	    nd->nl_flags &= ~NLC_NCPISLOCKED;
	    cache_unlock(&nd->nl_nch);
	}
	cache_drop(&nd->nl_nch);
    }
    if (nd->nl_rootnch.ncp)
	cache_drop(&nd->nl_rootnch);
    if (nd->nl_jailnch.ncp)
	cache_drop(&nd->nl_jailnch);
    if ((nd->nl_flags & NLC_HASBUF) && nd->nl_path) {
	objcache_put(namei_oc, nd->nl_path);
	nd->nl_path = NULL;
    }
    if (nd->nl_cred) {
	crfree(nd->nl_cred);
	nd->nl_cred = NULL;
    }
    if (nd->nl_open_vp) {
	if (nd->nl_flags & NLC_LOCKVP) {
		vn_unlock(nd->nl_open_vp);
		nd->nl_flags &= ~NLC_LOCKVP;
	}
	vn_close(nd->nl_open_vp, nd->nl_vp_fmode);
	nd->nl_open_vp = NULL;
    }
    nd->nl_flags = 0;	/* clear remaining flags (just clear everything) */
}

void
nlookup_zero(struct nlookupdata *nd)
{
    bzero(nd, sizeof(struct nlookupdata));
}

/*
 * Simple all-in-one nlookup.  Returns a locked namecache structure or NULL
 * if an error occured. 
 *
 * Note that the returned ncp is not checked for permissions, though VEXEC
 * is checked on the directory path leading up to the result.  The caller
 * must call naccess() to check the permissions of the returned leaf.
 */
struct nchandle
nlookup_simple(const char *str, enum uio_seg seg,
	       int niflags, int *error)
{
    struct nlookupdata nd;
    struct nchandle nch;

    *error = nlookup_init(&nd, str, seg, niflags);
    if (*error == 0) {
	    if ((*error = nlookup(&nd)) == 0) {
		    nch = nd.nl_nch;	/* keep hold ref from structure */
		    cache_zero(&nd.nl_nch); /* and NULL out */
	    } else {
		    cache_zero(&nch);
	    }
	    nlookup_done(&nd);
    } else {
	    cache_zero(&nch);
    }
    return(nch);
}

/*
 * Do a generic nlookup.  Note that the passed nd is not nlookup_done()'d
 * on return, even if an error occurs.  If no error occurs the returned
 * nl_nch is always referenced and locked, otherwise it may or may not be.
 *
 * Intermediate directory elements, including the current directory, require
 * execute (search) permission.  nlookup does not examine the access 
 * permissions on the returned element.
 *
 * If NLC_CREATE or NLC_DELETE is set the last directory must allow node
 * creation (VCREATE/VDELETE), and an error code of 0 will be returned for
 * a non-existant target.  Otherwise a non-existant target will cause
 * ENOENT to be returned.
 */
int
nlookup(struct nlookupdata *nd)
{
    struct nlcomponent nlc;
    struct nchandle nch;
    struct mount *mp;
    int wasdotordotdot;
    char *ptr;
    char *xptr;
    int error;
    int len;

#ifdef KTRACE
    if (KTRPOINT(nd->nl_td, KTR_NAMEI))
	ktrnamei(nd->nl_td->td_proc, nd->nl_path);
#endif
    bzero(&nlc, sizeof(nlc));

    /*
     * Setup for the loop.  The current working namecache element must
     * be in a refd + unlocked state.  This typically the case on entry except
     * when stringing nlookup()'s along in a chain, since nlookup() always
     * returns nl_nch in a locked state.
     */
    nd->nl_loopcnt = 0;
    if (nd->nl_flags & NLC_NCPISLOCKED) {
	nd->nl_flags &= ~NLC_NCPISLOCKED;
	cache_unlock(&nd->nl_nch);
    }
    ptr = nd->nl_path;

    /*
     * Loop on the path components.  At the top of the loop nd->nl_nch
     * is ref'd and unlocked and represents our current position.
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
	    cache_copy(&nd->nl_rootnch, &nch);
	    cache_drop(&nd->nl_nch);
	    nd->nl_nch = nch;
	    if (*ptr == 0) {
		cache_lock(&nd->nl_nch);
		nd->nl_flags |= NLC_NCPISLOCKED;
		error = 0;
		break;
	    }
	    continue;
	}

	/*
	 * Check directory search permissions.
	 */
	if ((error = naccess(&nd->nl_nch, VEXEC, nd->nl_cred)) != 0)
	    break;

	/*
	 * Extract the path component
	 */
	nlc.nlc_nameptr = ptr;
	while (*ptr && *ptr != '/')
	    ++ptr;
	nlc.nlc_namelen = ptr - nlc.nlc_nameptr;

	/*
	 * Lookup the path component in the cache, creating an unresolved
	 * entry if necessary.  We have to handle "." and ".." as special
	 * cases.
	 *
	 * When handling ".." we have to detect a traversal back through a
	 * mount point.   If we are at the root, ".." just returns the root.
	 *
	 * This subsection returns a locked, refd 'nch' unless it errors out.
	 * The namecache topology is not allowed to be disconnected, so 
	 * encountering a NULL parent will generate EINVAL.  This typically
	 * occurs when a directory is removed out from under a process.
	 *
	 * If NLC_DELETE is set neither '.' or '..' can be the last component
	 * of a path.
	 */
	if (nlc.nlc_namelen == 1 && nlc.nlc_nameptr[0] == '.') {
	    cache_get(&nd->nl_nch, &nch);
	    wasdotordotdot = 1;
	} else if (nlc.nlc_namelen == 2 && 
		   nlc.nlc_nameptr[0] == '.' && nlc.nlc_nameptr[1] == '.') {
	    if (nd->nl_nch.mount == nd->nl_rootnch.mount &&
		nd->nl_nch.ncp == nd->nl_rootnch.ncp
	    ) {
		/*
		 * ".." at the root returns the root
		 */
		cache_get(&nd->nl_nch, &nch);
	    } else {
		/*
		 * Locate the parent ncp.  If we are at the root of a
		 * filesystem mount we have to skip to the mounted-on
		 * point in the underlying filesystem.
		 */
		nch = nd->nl_nch;
		while (nch.ncp == nch.mount->mnt_ncmountpt.ncp)
			nch = nd->nl_nch.mount->mnt_ncmounton;
		nch.ncp = nch.ncp->nc_parent;
		KKASSERT(nch.ncp != NULL);
		cache_get(&nch, &nch);
	    }
	    wasdotordotdot = 1;
	} else {
	    nch = cache_nlookup(&nd->nl_nch, &nlc);
	    while ((error = cache_resolve(&nch, nd->nl_cred)) == EAGAIN) {
		printf("[diagnostic] nlookup: relookup %*.*s\n", 
			nch.ncp->nc_nlen, nch.ncp->nc_nlen, nch.ncp->nc_name);
		cache_put(&nch);
		nch = cache_nlookup(&nd->nl_nch, &nlc);
	    }
	    wasdotordotdot = 0;
	}
	/*
	 * [end of subsection] ncp is locked and ref'd.  nd->nl_nch is ref'd
	 */

	/*
	 * Resolve the namespace if necessary.  The ncp returned by
	 * cache_nlookup() is referenced and locked.
	 *
	 * XXX neither '.' nor '..' should return EAGAIN since they were
	 * previously resolved and thus cannot be newly created ncp's.
	 */
	if (nch.ncp->nc_flag & NCF_UNRESOLVED) {
	    error = cache_resolve(&nch, nd->nl_cred);
	    KKASSERT(error != EAGAIN);
	} else {
	    error = nch.ncp->nc_error;
	}

	/*
	 * Early completion.  ENOENT is not an error if this is the last
	 * component and NLC_CREATE was requested.  Note that ncp->nc_error
	 * is left as ENOENT in that case, which we check later on.
	 *
	 * Also handle invalid '.' or '..' components terminating a path
	 * during removal.  The standard requires this and pax pretty
	  *stupidly depends on it.
	 */
	for (xptr = ptr; *xptr == '/'; ++xptr)
		;
	if (*xptr == 0) {
	    if (error == ENOENT && (nd->nl_flags & NLC_CREATE))
		error = naccess(&nch, VCREATE, nd->nl_cred);
	    if (error == 0 && wasdotordotdot && (nd->nl_flags & NLC_DELETE))
		error = EINVAL;
	}

	/*
	 * Early completion on error.
	 */
	if (error) {
	    cache_put(&nch);
	    break;
	}

	/*
	 * If the element is a symlink and it is either not the last
	 * element or it is the last element and we are allowed to
	 * follow symlinks, resolve the symlink.
	 */
	if ((nch.ncp->nc_flag & NCF_ISSYMLINK) &&
	    (*ptr || (nd->nl_flags & NLC_FOLLOW))
	) {
	    if (nd->nl_loopcnt++ >= MAXSYMLINKS) {
		error = ELOOP;
		cache_put(&nch);
		break;
	    }
	    error = nreadsymlink(nd, &nch, &nlc);
	    cache_put(&nch);
	    if (error)
		break;

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
		objcache_put(namei_oc, nlc.nlc_nameptr);
		break;
	    }
	    bcopy(ptr, nlc.nlc_nameptr + nlc.nlc_namelen, len + 1);
	    if (nd->nl_flags & NLC_HASBUF)
		objcache_put(namei_oc, nd->nl_path);
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
	 * If the element is a directory and we are crossing a mount point,
	 * Locate the mount.
	 */
	while ((nch.ncp->nc_flag & NCF_ISMOUNTPT) && 
	    (nd->nl_flags & NLC_NOCROSSMOUNT) == 0 &&
	    (mp = cache_findmount(&nch)) != NULL
	) {
	    struct vnode *tdp;

	    cache_put(&nch);
	    cache_get(&mp->mnt_ncmountpt, &nch);

	    if (nch.ncp->nc_flag & NCF_UNRESOLVED) {
		while (vfs_busy(mp, 0))
		    ;
		error = VFS_ROOT(mp, &tdp);
		vfs_unbusy(mp);
		if (error)
		    break;
		cache_setvp(&nch, tdp);
		vput(tdp);
	    }
	}
	if (error) {
	    cache_put(&nch);
	    break;
	}
	    
	/*
	 * Skip any slashes to get to the next element.  If there 
	 * are any slashes at all the current element must be a
	 * directory or, in the create case, intended to become a directory.
	 * If it isn't we break without incrementing ptr and fall through
	 * to the failure case below.
	 */
	while (*ptr == '/') {
	    if ((nch.ncp->nc_flag & NCF_ISDIR) == 0 && 
		!(nd->nl_flags & NLC_WILLBEDIR)
	    ) {
		break;
	    }
	    ++ptr;
	}

	/*
	 * Continuation case: additional elements and the current
	 * element is a directory.
	 */
	if (*ptr && (nch.ncp->nc_flag & NCF_ISDIR)) {
	    cache_drop(&nd->nl_nch);
	    cache_unlock(&nch);
	    nd->nl_nch = nch;
	    continue;
	}

	/*
	 * Failure case: additional elements and the current element
	 * is not a directory
	 */
	if (*ptr) {
	    cache_put(&nch);
	    error = ENOTDIR;
	    break;
	}

	/*
	 * Successful lookup of last element.
	 *
	 * Check directory permissions if a deletion is specified.
	 */
	if (*ptr == 0 && (nd->nl_flags & NLC_DELETE)) {
	    if ((error = naccess(&nch, VDELETE, nd->nl_cred)) != 0) {
		cache_put(&nch);
		break;
	    }
	}

	/*
	 * Termination: no more elements.  If NLC_CREATE was set the
	 * ncp may represent a negative hit (ncp->nc_error will be ENOENT),
	 * but we still return an error code of 0.
	 */
	cache_drop(&nd->nl_nch);
	nd->nl_nch = nch;
	nd->nl_flags |= NLC_NCPISLOCKED;
	error = 0;
	break;
    }
    return(error);
}

/*
 * Resolve a mount point's glue ncp.  This ncp connects creates the illusion
 * of continuity in the namecache tree by connecting the ncp related to the
 * vnode under the mount to the ncp related to the mount's root vnode.
 *
 * If no error occured a locked, ref'd ncp is stored in *ncpp.
 */
int
nlookup_mp(struct mount *mp, struct nchandle *nch)
{
    struct vnode *vp;
    int error;

    error = 0;
    cache_get(&mp->mnt_ncmountpt, nch);
    if (nch->ncp->nc_flag & NCF_UNRESOLVED) {
	while (vfs_busy(mp, 0))
	    ;
	error = VFS_ROOT(mp, &vp);
	vfs_unbusy(mp);
	if (error) {
	    cache_put(nch);
	} else {
	    cache_setvp(nch, vp);
	    vput(vp);
	}
    }
    return(error);
}

/*
 * Read the contents of a symlink, allocate a path buffer out of the
 * namei_oc and initialize the supplied nlcomponent with the result.
 *
 * If an error occurs no buffer will be allocated or returned in the nlc.
 */
int
nreadsymlink(struct nlookupdata *nd, struct nchandle *nch, 
		struct nlcomponent *nlc)
{
    struct vnode *vp;
    struct iovec aiov;
    struct uio auio;
    int linklen;
    int error;
    char *cp;

    nlc->nlc_nameptr = NULL;
    nlc->nlc_namelen = 0;
    if (nch->ncp->nc_vp == NULL)
	return(ENOENT);
    if ((error = cache_vget(nch, nd->nl_cred, LK_SHARED, &vp)) != 0)
	return(error);
    cp = objcache_get(namei_oc, M_WAITOK);
    aiov.iov_base = cp;
    aiov.iov_len = MAXPATHLEN;
    auio.uio_iov = &aiov;
    auio.uio_iovcnt = 1;
    auio.uio_offset = 0;
    auio.uio_rw = UIO_READ;
    auio.uio_segflg = UIO_SYSSPACE;
    auio.uio_td = nd->nl_td;
    auio.uio_resid = MAXPATHLEN - 1;
    error = VOP_READLINK(vp, &auio, nd->nl_cred);
    if (error)
	goto fail;
    linklen = MAXPATHLEN - 1 - auio.uio_resid;
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
    vput(vp);
    return(0);
fail:
    objcache_put(namei_oc, cp);
    vput(vp);
    return(error);
}

/*
 * Check access [XXX cache vattr!] [XXX quota]
 *
 * Generally check the V* access bits from sys/vnode.h.  All specified bits
 * must pass for this function to return 0.
 *
 * If VCREATE is specified and the target ncp represents a non-existant
 * file or dir, or if VDELETE is specified and the target exists, the parent
 * directory is checked for VWRITE.  If VEXCL is specified and the target
 * ncp represents a positive hit, an error is returned.
 *
 * If VCREATE is not specified and the target does not exist (negative hit),
 * ENOENT is returned.  Note that nlookup() does not (and should not) return
 * ENOENT for non-existant leafs.
 *
 * The passed ncp may or may not be locked.  The caller should use a
 * locked ncp on leaf lookups, especially for VCREATE, VDELETE, and VEXCL
 * checks.
 */
int
naccess(struct nchandle *nch, int vmode, struct ucred *cred)
{
    struct nchandle par;
    struct vnode *vp;
    struct vattr va;
    int error;

    if (nch->ncp->nc_flag & NCF_UNRESOLVED) {
	cache_lock(nch);
	cache_resolve(nch, cred);
	cache_unlock(nch);
    }
    error = nch->ncp->nc_error;
    if (vmode & (VDELETE|VCREATE|VEXCL)) {
	if (((vmode & VCREATE) && nch->ncp->nc_vp == NULL) ||
	    ((vmode & VDELETE) && nch->ncp->nc_vp != NULL)
	) {
	    if ((par.ncp = nch->ncp->nc_parent) == NULL) {
		if (error != EAGAIN)
			error = EINVAL;
	    } else {
		par.mount = nch->mount;
		cache_hold(&par);
		error = naccess(&par, VWRITE, cred);
		cache_drop(&par);
	    }
	}
	if ((vmode & VEXCL) && nch->ncp->nc_vp != NULL)
	    error = EEXIST;
    }
    if (error == 0) {
	error = cache_vget(nch, cred, LK_SHARED, &vp);
	if (error == ENOENT) {
	    if (vmode & VCREATE)
		error = 0;
	} else if (error == 0) {
	    /* XXX cache the va in the namecache or in the vnode */
	    if ((error = VOP_GETATTR(vp, &va)) == 0) {
		if ((vmode & VWRITE) && vp->v_mount) {
		    if (vp->v_mount->mnt_flag & MNT_RDONLY)
			error = EROFS;
		}
	    }
	    vput(vp);
	    if (error == 0)
		error = naccess_va(&va, vmode, cred);
	}
    }
    return(error);
}

/*
 * Check the requested access against the given vattr using cred.
 */
int
naccess_va(struct vattr *va, int vmode, struct ucred *cred)
{
    int i;

    /*
     * Test the immutable bit for files, directories, and softlinks.
     */
    if (vmode & (VWRITE|VDELETE)) {
	if (va->va_type == VDIR || va->va_type == VLNK || va->va_type == VREG) {
	    if (va->va_flags & IMMUTABLE)
		return (EPERM);
	}
    }

    /*
     * root gets universal access
     */
    if (cred->cr_uid == 0)
	return(0);

    /*
     * Check owner perms, group perms, and world perms
     */
    vmode &= S_IRWXU;
    if (cred->cr_uid == va->va_uid) {
	if ((vmode & va->va_mode) != vmode)
	    return(EACCES);
	return(0);
    }

    vmode >>= 3;
    for (i = 0; i < cred->cr_ngroups; ++i) {
	if (va->va_gid == cred->cr_groups[i]) {
	    if ((vmode & va->va_mode) != vmode)
		return(EACCES);
	    return(0);
	}
    }

    vmode >>= 3;
    if ((vmode & va->va_mode) != vmode)
	return(EACCES);
    return(0);
}

