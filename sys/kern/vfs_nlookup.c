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
#include <sys/file.h>

#ifdef KTRACE
#include <sys/ktrace.h>
#endif

static int naccess(struct nchandle *nch, int vmode, struct ucred *cred,
		int *stickyp);

/*
 * Initialize a nlookup() structure, early error return for copyin faults
 * or a degenerate empty string (which is not allowed).
 *
 * The first process proc0's credentials are used if the calling thread
 * is not associated with a process context.
 *
 * MPSAFE
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
 * nlookup_init() for "at" family of syscalls.
 *
 * Works similarly to nlookup_init() but if path is relative and fd is not
 * AT_FDCWD, path is interpreted relative to the directory pointed to by fd.
 * In this case, the file entry pointed to by fd is ref'ed and returned in
 * *fpp. 
 *
 * If the call succeeds, nlookup_done_at() must be called to clean-up the nd
 * and release the ref to the file entry.
 */
int
nlookup_init_at(struct nlookupdata *nd, struct file **fpp, int fd, 
		const char *path, enum uio_seg seg, int flags)
{
	struct thread *td = curthread;
	struct proc *p = td->td_proc;
	struct file* fp;
	struct vnode *vp;
	int error;

	*fpp = NULL;

	if  ((error = nlookup_init(nd, path, seg, flags)) != 0) {
		return (error);
	}

	if (nd->nl_path[0] != '/' && fd != AT_FDCWD) {
		if ((error = holdvnode(p->p_fd, fd, &fp)) != 0)
			goto done;
		vp = (struct vnode*)fp->f_data;
		if (vp->v_type != VDIR || fp->f_nchandle.ncp == NULL) {
			fdrop(fp);
			fp = NULL;
			error = ENOTDIR;
			goto done;
		}
		cache_drop(&nd->nl_nch);
		cache_copy(&fp->f_nchandle, &nd->nl_nch);
		*fpp = fp;
	}


done:
	if (error)
		nlookup_done(nd);
	return (error);

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
 * This works similarly to nlookup_init_raw() but does not rely
 * on rootnch being initialized yet.
 */
int
nlookup_init_root(struct nlookupdata *nd, 
	     const char *path, enum uio_seg seg, int flags,
	     struct ucred *cred, struct nchandle *ncstart,
	     struct nchandle *ncroot)
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
	cache_copy(ncroot, &nd->nl_rootnch);
	cache_copy(ncroot, &nd->nl_jailnch);
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
	cache_drop(&nd->nl_nch);	/* NULL's out the nch */
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
	vn_close(nd->nl_open_vp, nd->nl_vp_fmode, NULL);
	nd->nl_open_vp = NULL;
    }
    if (nd->nl_dvp) {
	vrele(nd->nl_dvp);
	nd->nl_dvp = NULL;
    }
    nd->nl_flags = 0;	/* clear remaining flags (just clear everything) */
}

/*
 * Works similarly to nlookup_done() when nd initialized with
 * nlookup_init_at().
 */
void
nlookup_done_at(struct nlookupdata *nd, struct file *fp)
{
	nlookup_done(nd);
	if (fp != NULL)
		fdrop(fp);
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
 * Returns non-zero if the path element is the last element
 */
static
int
islastelement(const char *ptr)
{
	while (*ptr == '/')
		++ptr;
	return (*ptr == 0);
}

/*
 * Returns non-zero if we need to lock the namecache element
 * exclusively.  Unless otherwise requested by NLC_SHAREDLOCK,
 * the last element of the namecache lookup will be locked
 * exclusively.
 *
 * NOTE: Even if we return on-zero, an unresolved namecache record
 *	 will always be locked exclusively.
 */
static __inline
int
wantsexcllock(struct nlookupdata *nd, const char *ptr)
{
	if ((nd->nl_flags & NLC_SHAREDLOCK) == 0)
		return(islastelement(ptr));
	return(0);
}


/*
 * Do a generic nlookup.  Note that the passed nd is not nlookup_done()'d
 * on return, even if an error occurs.  If no error occurs or NLC_CREATE
 * is flagged and ENOENT is returned, then the returned nl_nch is always
 * referenced and locked exclusively.
 *
 * WARNING: For any general error other than ENOENT w/NLC_CREATE, the
 *	    the resulting nl_nch may or may not be locked and if locked
 *	    might be locked either shared or exclusive.
 *
 * Intermediate directory elements, including the current directory, require
 * execute (search) permission.  nlookup does not examine the access 
 * permissions on the returned element.
 *
 * If NLC_CREATE is set the last directory must allow node creation,
 * and an error code of 0 will be returned for a non-existant
 * target (not ENOENT).
 *
 * If NLC_RENAME_DST is set the last directory mut allow node deletion,
 * plus the sticky check is made, and an error code of 0 will be returned
 * for a non-existant target (not ENOENT).
 *
 * If NLC_DELETE is set the last directory mut allow node deletion,
 * plus the sticky check is made.
 *
 * If NLC_REFDVP is set nd->nl_dvp will be set to the directory vnode
 * of the returned entry.  The vnode will be referenced, but not locked,
 * and will be released by nlookup_done() along with everything else.
 *
 * NOTE: As an optimization we attempt to obtain a shared namecache lock
 *	 on any intermediate elements.  On success, the returned element
 *	 is ALWAYS locked exclusively.
 */
int
nlookup(struct nlookupdata *nd)
{
    globaldata_t gd = mycpu;
    struct nlcomponent nlc;
    struct nchandle nch;
    struct nchandle par;
    struct nchandle nctmp;
    struct mount *mp;
    struct vnode *hvp;		/* hold to prevent recyclement */
    int wasdotordotdot;
    char *ptr;
    int error;
    int len;
    int dflags;
    int hit = 1;

#ifdef KTRACE
    if (KTRPOINT(nd->nl_td, KTR_NAMEI))
	ktrnamei(nd->nl_td->td_lwp, nd->nl_path);
#endif
    bzero(&nlc, sizeof(nlc));

    /*
     * Setup for the loop.  The current working namecache element is
     * always at least referenced.  We lock it as required, but always
     * return a locked, resolved namecache entry.
     */
    nd->nl_loopcnt = 0;
    if (nd->nl_dvp) {
	vrele(nd->nl_dvp);
	nd->nl_dvp = NULL;
    }
    ptr = nd->nl_path;

    /*
     * Loop on the path components.  At the top of the loop nd->nl_nch
     * is ref'd and unlocked and represents our current position.
     */
    for (;;) {
	/*
	 * Make sure nl_nch is locked so we can access the vnode, resolution
	 * state, etc.
	 */
	if ((nd->nl_flags & NLC_NCPISLOCKED) == 0) {
		nd->nl_flags |= NLC_NCPISLOCKED;
		cache_lock_maybe_shared(&nd->nl_nch, wantsexcllock(nd, ptr));
	}

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
	    cache_unlock(&nd->nl_nch);
	    cache_get_maybe_shared(&nd->nl_rootnch, &nch,
				   wantsexcllock(nd, ptr));
	    cache_drop(&nd->nl_nch);
	    nd->nl_nch = nch;		/* remains locked */

	    /*
	     * Fast-track termination.  There is no parent directory of
	     * the root in the same mount from the point of view of
	     * the caller so return EACCES if NLC_REFDVP is specified,
	     * and EEXIST if NLC_CREATE is also specified.
	     * e.g. 'rmdir /' or 'mkdir /' are not allowed.
	     */
	    if (*ptr == 0) {
		if (nd->nl_flags & NLC_REFDVP)
			error = (nd->nl_flags & NLC_CREATE) ? EEXIST : EACCES;
		else
			error = 0;
		break;
	    }
	    continue;
	}

	/*
	 * Check directory search permissions (nd->nl_nch is locked & refd)
	 */
	dflags = 0;
	error = naccess(&nd->nl_nch, NLC_EXEC, nd->nl_cred, &dflags);
	if (error)
	    break;

	/*
	 * Extract the path component.  Path components are limited to
	 * 255 characters.
	 */
	nlc.nlc_nameptr = ptr;
	while (*ptr && *ptr != '/')
	    ++ptr;
	nlc.nlc_namelen = ptr - nlc.nlc_nameptr;
	if (nlc.nlc_namelen >= 256) {
	    error = ENAMETOOLONG;
	    break;
	}

	/*
	 * Lookup the path component in the cache, creating an unresolved
	 * entry if necessary.  We have to handle "." and ".." as special
	 * cases.
	 *
	 * When handling ".." we have to detect a traversal back through a
	 * mount point.   If we are at the root, ".." just returns the root.
	 *
	 * When handling "." or ".." we also have to recalculate dflags
	 * since our dflags will be for some sub-directory instead of the
	 * parent dir.
	 *
	 * This subsection returns a locked, refd 'nch' unless it errors out,
	 * and an unlocked but still ref'd nd->nl_nch.
	 *
	 * The namecache topology is not allowed to be disconnected, so 
	 * encountering a NULL parent will generate EINVAL.  This typically
	 * occurs when a directory is removed out from under a process.
	 *
	 * WARNING! The unlocking of nd->nl_nch is sensitive code.
	 */
	KKASSERT(nd->nl_flags & NLC_NCPISLOCKED);

	if (nlc.nlc_namelen == 1 && nlc.nlc_nameptr[0] == '.') {
	    cache_unlock(&nd->nl_nch);
	    nd->nl_flags &= ~NLC_NCPISLOCKED;
	    cache_get_maybe_shared(&nd->nl_nch, &nch, wantsexcllock(nd, ptr));
	    wasdotordotdot = 1;
	} else if (nlc.nlc_namelen == 2 && 
		   nlc.nlc_nameptr[0] == '.' && nlc.nlc_nameptr[1] == '.') {
	    if (nd->nl_nch.mount == nd->nl_rootnch.mount &&
		nd->nl_nch.ncp == nd->nl_rootnch.ncp
	    ) {
		/*
		 * ".." at the root returns the root
		 */
		cache_unlock(&nd->nl_nch);
		nd->nl_flags &= ~NLC_NCPISLOCKED;
		cache_get_maybe_shared(&nd->nl_nch, &nch,
				       wantsexcllock(nd, ptr));
	    } else {
		/*
		 * Locate the parent ncp.  If we are at the root of a
		 * filesystem mount we have to skip to the mounted-on
		 * point in the underlying filesystem.
		 *
		 * Expect the parent to always be good since the
		 * mountpoint doesn't go away.  XXX hack.  cache_get()
		 * requires the ncp to already have a ref as a safety.
		 *
		 * However, a process which has been broken out of a chroot
		 * will wind up with a NULL parent if it tries to '..' above
		 * the real root, deal with the case.  Note that this does
		 * not protect us from a jail breakout, it just stops a panic
		 * if the jail-broken process tries to '..' past the real
		 * root.
		 */
		nctmp = nd->nl_nch;
		while (nctmp.ncp == nctmp.mount->mnt_ncmountpt.ncp) {
			nctmp = nctmp.mount->mnt_ncmounton;
			if (nctmp.ncp == NULL)
				break;
		}
		if (nctmp.ncp == NULL) {
			if (curthread->td_proc) {
				kprintf("vfs_nlookup: '..' traverse broke "
					"jail: pid %d (%s)\n",
					curthread->td_proc->p_pid,
					curthread->td_comm);
			}
			nctmp = nd->nl_rootnch;
		} else {
			nctmp.ncp = nctmp.ncp->nc_parent;
		}
		cache_hold(&nctmp);
		cache_unlock(&nd->nl_nch);
		nd->nl_flags &= ~NLC_NCPISLOCKED;
		cache_get_maybe_shared(&nctmp, &nch, wantsexcllock(nd, ptr));
		cache_drop(&nctmp);		/* NOTE: zero's nctmp */
	    }
	    wasdotordotdot = 2;
	} else {
	    /*
	     * Must unlock nl_nch when traversing down the path.  However,
	     * the child ncp has not yet been found/created and the parent's
	     * child list might be empty.  Thus releasing the lock can
	     * allow a race whereby the parent ncp's vnode is recycled.
	     * This case can occur especially when maxvnodes is set very low.
	     *
	     * We need the parent's ncp to remain resolved for all normal
	     * filesystem activities, so we vhold() the vp during the lookup
	     * to prevent recyclement due to vnlru / maxvnodes.
	     *
	     * If we race an unlink or rename the ncp might be marked
	     * DESTROYED after resolution, requiring a retry.
	     */
	    if ((hvp = nd->nl_nch.ncp->nc_vp) != NULL)
		vhold(hvp);
	    cache_unlock(&nd->nl_nch);
	    nd->nl_flags &= ~NLC_NCPISLOCKED;
	    error = cache_nlookup_maybe_shared(&nd->nl_nch, &nlc,
					       wantsexcllock(nd, ptr), &nch);
	    if (error == EWOULDBLOCK) {
		    nch = cache_nlookup(&nd->nl_nch, &nlc);
		    if (nch.ncp->nc_flag & NCF_UNRESOLVED)
			hit = 0;
		    for (;;) {
			error = cache_resolve(&nch, nd->nl_cred);
			if (error != EAGAIN &&
			    (nch.ncp->nc_flag & NCF_DESTROYED) == 0) {
				break;
			}
			kprintf("[diagnostic] nlookup: relookup %*.*s\n",
				nch.ncp->nc_nlen, nch.ncp->nc_nlen,
				nch.ncp->nc_name);
			cache_put(&nch);
			nch = cache_nlookup(&nd->nl_nch, &nlc);
		    }
	    }
	    if (hvp)
		vdrop(hvp);
	    wasdotordotdot = 0;
	}

	/*
	 * If the last component was "." or ".." our dflags no longer
	 * represents the parent directory and we have to explicitly
	 * look it up.
	 *
	 * Expect the parent to be good since nch is locked.
	 */
	if (wasdotordotdot && error == 0) {
	    dflags = 0;
	    if ((par.ncp = nch.ncp->nc_parent) != NULL) {
		par.mount = nch.mount;
		cache_hold(&par);
		cache_lock_maybe_shared(&par, wantsexcllock(nd, ptr));
		error = naccess(&par, 0, nd->nl_cred, &dflags);
		cache_put(&par);
	    }
	}

	/*
	 * [end of subsection]
	 *
	 * nch is locked and referenced.
	 * nd->nl_nch is unlocked and referenced.
	 *
	 * nl_nch must be unlocked or we could chain lock to the root
	 * if a resolve gets stuck (e.g. in NFS).
	 */
	KKASSERT((nd->nl_flags & NLC_NCPISLOCKED) == 0);

	/*
	 * Resolve the namespace if necessary.  The ncp returned by
	 * cache_nlookup() is referenced and locked.
	 *
	 * XXX neither '.' nor '..' should return EAGAIN since they were
	 * previously resolved and thus cannot be newly created ncp's.
	 */
	if (nch.ncp->nc_flag & NCF_UNRESOLVED) {
	    hit = 0;
	    error = cache_resolve(&nch, nd->nl_cred);
	    KKASSERT(error != EAGAIN);
	} else {
	    error = nch.ncp->nc_error;
	}

	/*
	 * Early completion.  ENOENT is not an error if this is the last
	 * component and NLC_CREATE or NLC_RENAME (rename target) was
	 * requested.  Note that ncp->nc_error is left as ENOENT in that
	 * case, which we check later on.
	 *
	 * Also handle invalid '.' or '..' components terminating a path
	 * for a create/rename/delete.  The standard requires this and pax
	 * pretty stupidly depends on it.
	 */
	if (islastelement(ptr)) {
	    if (error == ENOENT &&
		(nd->nl_flags & (NLC_CREATE | NLC_RENAME_DST))
	    ) {
		if (nd->nl_flags & NLC_NFS_RDONLY) {
			error = EROFS;
		} else {
			error = naccess(&nch, nd->nl_flags | dflags,
					nd->nl_cred, NULL);
		}
	    }
	    if (error == 0 && wasdotordotdot &&
		(nd->nl_flags & (NLC_CREATE | NLC_DELETE |
				 NLC_RENAME_SRC | NLC_RENAME_DST))) {
		/*
		 * POSIX junk
		 */
		if (nd->nl_flags & NLC_CREATE)
			error = EEXIST;
		else if (nd->nl_flags & NLC_DELETE)
			error = (wasdotordotdot == 1) ? EINVAL : ENOTEMPTY;
		else
			error = EINVAL;
	    }
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
	    int vfs_do_busy = 0;

	    /*
	     * VFS must be busied before the namecache entry is locked,
	     * but we don't want to waste time calling vfs_busy() if the
	     * mount point is already resolved.
	     */
again:
	    cache_put(&nch);
	    if (vfs_do_busy) {
		while (vfs_busy(mp, 0)) {
		    if (mp->mnt_kern_flag & MNTK_UNMOUNT) {
			kprintf("nlookup: warning umount race avoided\n");
			cache_dropmount(mp);
			error = EBUSY;
			vfs_do_busy = 0;
			goto double_break;
		    }
		}
	    }
	    cache_get_maybe_shared(&mp->mnt_ncmountpt, &nch,
				   wantsexcllock(nd, ptr));

	    if (nch.ncp->nc_flag & NCF_UNRESOLVED) {
		if (vfs_do_busy == 0) {
		    vfs_do_busy = 1;
		    goto again;
		}
		error = VFS_ROOT(mp, &tdp);
		vfs_unbusy(mp);
		vfs_do_busy = 0;
		if (error) {
		    cache_dropmount(mp);
		    break;
		}
		cache_setvp(&nch, tdp);
		vput(tdp);
	    }
	    if (vfs_do_busy)
		vfs_unbusy(mp);
	    cache_dropmount(mp);
	}

	if (error) {
	    cache_put(&nch);
double_break:
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
	    KKASSERT((nd->nl_flags & NLC_NCPISLOCKED) == 0);
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
	 * Check permissions if the target exists.  If the target does not
	 * exist directory permissions were already tested in the early
	 * completion code above.
	 *
	 * nd->nl_flags will be adjusted on return with NLC_APPENDONLY
	 * if the file is marked append-only, and NLC_STICKY if the directory
	 * containing the file is sticky.
	 */
	if (nch.ncp->nc_vp && (nd->nl_flags & NLC_ALLCHKS)) {
	    error = naccess(&nch, nd->nl_flags | dflags,
			    nd->nl_cred, NULL);
	    if (error) {
		cache_put(&nch);
		break;
	    }
	}

	/*
	 * Termination: no more elements.
	 *
	 * If NLC_REFDVP is set acquire a referenced parent dvp.
	 */
	if (nd->nl_flags & NLC_REFDVP) {
		cache_lock(&nd->nl_nch);
		error = cache_vref(&nd->nl_nch, nd->nl_cred, &nd->nl_dvp);
		cache_unlock(&nd->nl_nch);
		if (error) {
			kprintf("NLC_REFDVP: Cannot ref dvp of %p\n", nch.ncp);
			cache_put(&nch);
			break;
		}
	}
	cache_drop(&nd->nl_nch);
	nd->nl_nch = nch;
	nd->nl_flags |= NLC_NCPISLOCKED;
	error = 0;
	break;
    }

    if (hit)
	++gd->gd_nchstats->ncs_longhits;
    else
	++gd->gd_nchstats->ncs_longmiss;

    if (nd->nl_flags & NLC_NCPISLOCKED)
	KKASSERT(cache_lockstatus(&nd->nl_nch) > 0);

    /*
     * NOTE: If NLC_CREATE was set the ncp may represent a negative hit
     * (ncp->nc_error will be ENOENT), but we will still return an error
     * code of 0.
     */
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
 * Generally check the NLC_* access bits.   All specified bits must pass
 * for this function to return 0.
 *
 * The file does not have to exist when checking NLC_CREATE or NLC_RENAME_DST
 * access, otherwise it must exist.  No error is returned in this case.
 *
 * The file must not exist if NLC_EXCL is specified.
 *
 * Directory permissions in general are tested for NLC_CREATE if the file
 * does not exist, NLC_DELETE if the file does exist, and NLC_RENAME_DST
 * whether the file exists or not.
 *
 * The directory sticky bit is tested for NLC_DELETE and NLC_RENAME_DST,
 * the latter is only tested if the target exists.
 *
 * The passed ncp must be referenced and locked.  If it is already resolved
 * it may be locked shared but otherwise should be locked exclusively.
 */
static int
naccess(struct nchandle *nch, int nflags, struct ucred *cred, int *nflagsp)
{
    struct vnode *vp;
    struct vattr va;
    struct namecache *ncp;
    int error;
    int cflags;

    KKASSERT(cache_lockstatus(nch) > 0);

    ncp = nch->ncp;
    if (ncp->nc_flag & NCF_UNRESOLVED) {
	cache_resolve(nch, cred);
	ncp = nch->ncp;
    }
    error = ncp->nc_error;

    /*
     * Directory permissions checks.  Silently ignore ENOENT if these
     * tests pass.  It isn't an error.
     *
     * We can safely resolve ncp->nc_parent because ncp is currently
     * locked.
     */
    if (nflags & (NLC_CREATE | NLC_DELETE | NLC_RENAME_SRC | NLC_RENAME_DST)) {
	if (((nflags & NLC_CREATE) && ncp->nc_vp == NULL) ||
	    ((nflags & NLC_DELETE) && ncp->nc_vp != NULL) ||
	    ((nflags & NLC_RENAME_SRC) && ncp->nc_vp != NULL) ||
	    (nflags & NLC_RENAME_DST)
	) {
	    struct nchandle par;

	    if ((par.ncp = ncp->nc_parent) == NULL) {
		if (error != EAGAIN)
			error = EINVAL;
	    } else if (error == 0 || error == ENOENT) {
		par.mount = nch->mount;
		cache_hold(&par);
		cache_lock_maybe_shared(&par, 0);
		error = naccess(&par, NLC_WRITE, cred, NULL);
		cache_put(&par);
	    }
	}
    }

    /*
     * NLC_EXCL check.  Target file must not exist.
     */
    if (error == 0 && (nflags & NLC_EXCL) && ncp->nc_vp != NULL)
	error = EEXIST;

    /*
     * Get the vnode attributes so we can do the rest of our checks.
     *
     * NOTE: We only call naccess_va() if the target exists.
     */
    if (error == 0) {
	error = cache_vget(nch, cred, LK_SHARED, &vp);
	if (error == ENOENT) {
	    /*
	     * Silently zero-out ENOENT if creating or renaming
	     * (rename target).  It isn't an error.
	     */
	    if (nflags & (NLC_CREATE | NLC_RENAME_DST))
		error = 0;
	} else if (error == 0) {
	    /*
	     * Get the vnode attributes and check for illegal O_TRUNC
	     * requests and read-only mounts.
	     *
	     * NOTE: You can still open devices on read-only mounts for
	     * 	     writing.
	     *
	     * NOTE: creates/deletes/renames are handled by the NLC_WRITE
	     *	     check on the parent directory above.
	     *
	     * XXX cache the va in the namecache or in the vnode
	     */
	    error = VOP_GETATTR(vp, &va);
	    if (error == 0 && (nflags & NLC_TRUNCATE)) {
		switch(va.va_type) {
		case VREG:
		case VDATABASE:
		case VCHR:
		case VBLK:
		case VFIFO:
		    break;
		case VDIR:
		    error = EISDIR;
		    break;
		default:
		    error = EINVAL;
		    break;
		}
	    }
	    if (error == 0 && (nflags & NLC_WRITE) && vp->v_mount &&
		(vp->v_mount->mnt_flag & MNT_RDONLY)
	    ) {
		switch(va.va_type) {
		case VDIR:
		case VLNK:
		case VREG:
		case VDATABASE:
		    error = EROFS;
		    break;
		default:
		    break;
		}
	    }
	    vput(vp);

	    /*
	     * Check permissions based on file attributes.  The passed
	     * flags (*nflagsp) are modified with feedback based on
	     * special attributes and requirements.
	     */
	    if (error == 0) {
		/*
		 * Adjust the returned (*nflagsp) if non-NULL.
		 */
		if (nflagsp) {
		    if ((va.va_mode & VSVTX) && va.va_uid != cred->cr_uid)
			*nflagsp |= NLC_STICKY;
		    if (va.va_flags & APPEND)
			*nflagsp |= NLC_APPENDONLY;
		    if (va.va_flags & IMMUTABLE)
			*nflagsp |= NLC_IMMUTABLE;
		}

		/*
		 * Track swapcache management flags in the namecache.
		 *
		 * Calculate the flags based on the current vattr info
		 * and recalculate the inherited flags from the parent
		 * (the original cache linkage may have occurred without
		 * getattrs and thus have stale flags).
		 */
		cflags = 0;
		if (va.va_flags & SF_NOCACHE)
			cflags |= NCF_SF_NOCACHE;
		if (va.va_flags & UF_CACHE)
			cflags |= NCF_UF_CACHE;
		if (ncp->nc_parent) {
			if (ncp->nc_parent->nc_flag &
			    (NCF_SF_NOCACHE | NCF_SF_PNOCACHE)) {
				cflags |= NCF_SF_PNOCACHE;
			}
			if (ncp->nc_parent->nc_flag &
			    (NCF_UF_CACHE | NCF_UF_PCACHE)) {
				cflags |= NCF_UF_PCACHE;
			}
		}

		/*
		 * XXX we're not supposed to update nc_flag when holding
		 *     a shared lock.
		 */
		atomic_clear_short(&ncp->nc_flag,
				   (NCF_SF_NOCACHE | NCF_UF_CACHE |
				   NCF_SF_PNOCACHE | NCF_UF_PCACHE) & ~cflags);
		atomic_set_short(&ncp->nc_flag, cflags);

		/*
		 * Process general access.
		 */
		error = naccess_va(&va, nflags, cred);
	    }
	}
    }
    return(error);
}

/*
 * Check the requested access against the given vattr using cred.
 */
int
naccess_va(struct vattr *va, int nflags, struct ucred *cred)
{
    int i;
    int vmode;

    /*
     * Test the immutable bit.  Creations, deletions, renames (source
     * or destination) are not allowed.  chown/chmod/other is also not
     * allowed but is handled by SETATTR.  Hardlinks to the immutable
     * file are allowed.
     *
     * If the directory is set to immutable then creations, deletions,
     * renames (source or dest) and hardlinks to files within the directory
     * are not allowed, and regular files opened through the directory may
     * not be written to or truncated (unless a special device).
     *
     * NOTE!  New hardlinks to immutable files work but new hardlinks to
     * files, immutable or not, sitting inside an immutable directory are
     * not allowed.  As always if the file is hardlinked via some other
     * path additional hardlinks may be possible even if the file is marked
     * immutable.  The sysop needs to create a closure by checking the hard
     * link count.  Once closure is achieved you are good, and security
     * scripts should check link counts anyway.
     *
     * Writes and truncations are only allowed on special devices.
     */
    if ((va->va_flags & IMMUTABLE) || (nflags & NLC_IMMUTABLE)) {
	if ((nflags & NLC_IMMUTABLE) && (nflags & NLC_HLINK))
	    return (EPERM);
	if (nflags & (NLC_CREATE | NLC_DELETE |
		      NLC_RENAME_SRC | NLC_RENAME_DST)) {
	    return (EPERM);
	}
	if (nflags & (NLC_WRITE | NLC_TRUNCATE)) {
	    switch(va->va_type) {
	    case VDIR:
		return (EISDIR);
	    case VLNK:
	    case VREG:
	    case VDATABASE:
		return (EPERM);
	    default:
		break;
	    }
	}
    }

    /*
     * Test the no-unlink and append-only bits for opens, rename targets,
     * and deletions.  These bits are not tested for creations or
     * rename sources.
     *
     * Unlike FreeBSD we allow a file with APPEND set to be renamed.
     * If you do not wish this you must also set NOUNLINK.
     *
     * If the governing directory is marked APPEND-only it implies
     * NOUNLINK for all entries in the directory.
     */
    if (((va->va_flags & NOUNLINK) || (nflags & NLC_APPENDONLY)) &&
	(nflags & (NLC_DELETE | NLC_RENAME_SRC | NLC_RENAME_DST))
    ) {
	return (EPERM);
    }

    /*
     * A file marked append-only may not be deleted but can be renamed.
     */
    if ((va->va_flags & APPEND) &&
	(nflags & (NLC_DELETE | NLC_RENAME_DST))
    ) {
	return (EPERM);
    }

    /*
     * A file marked append-only which is opened for writing must also
     * be opened O_APPEND.
     */
    if ((va->va_flags & APPEND) && (nflags & (NLC_OPEN | NLC_TRUNCATE))) {
	if (nflags & NLC_TRUNCATE)
	    return (EPERM);
	if ((nflags & (NLC_OPEN | NLC_WRITE)) == (NLC_OPEN | NLC_WRITE)) {
	    if ((nflags & NLC_APPEND) == 0)
		return (EPERM);
	}
    }

    /*
     * root gets universal access
     */
    if (cred->cr_uid == 0)
	return(0);

    /*
     * Check owner perms.
     *
     * If NLC_OWN is set the owner of the file is allowed no matter when
     * the owner-mode bits say (utimes).
     */
    vmode = 0;
    if (nflags & NLC_READ)
	vmode |= S_IRUSR;
    if (nflags & NLC_WRITE)
	vmode |= S_IWUSR;
    if (nflags & NLC_EXEC)
	vmode |= S_IXUSR;

    if (cred->cr_uid == va->va_uid) {
	if ((nflags & NLC_OWN) == 0) {
	    if ((vmode & va->va_mode) != vmode)
		return(EACCES);
	}
	return(0);
    }

    /*
     * If NLC_STICKY is set only the owner may delete or rename a file.
     * This bit is typically set on /tmp.
     *
     * Note that the NLC_READ/WRITE/EXEC bits are not typically set in
     * the specific delete or rename case.  For deletions and renames we
     * usually just care about directory permissions, not file permissions.
     */
    if ((nflags & NLC_STICKY) &&
	(nflags & (NLC_RENAME_SRC | NLC_RENAME_DST | NLC_DELETE))) {
	return(EACCES);
    }

    /*
     * Check group perms
     */
    vmode >>= 3;
    for (i = 0; i < cred->cr_ngroups; ++i) {
	if (va->va_gid == cred->cr_groups[i]) {
	    if ((vmode & va->va_mode) != vmode)
		return(EACCES);
	    return(0);
	}
    }

    /*
     * Check world perms
     */
    vmode >>= 3;
    if ((vmode & va->va_mode) != vmode)
	return(EACCES);
    return(0);
}

