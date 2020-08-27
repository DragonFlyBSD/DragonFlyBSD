/*
 * Copyright (c) 2004-2020 The DragonFly Project.  All rights reserved.
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
#include <sys/uio.h>
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
#include <sys/kcollect.h>
#include <sys/sysctl.h>

#ifdef KTRACE
#include <sys/ktrace.h>
#endif

__read_mostly static int nlookup_debug;
SYSCTL_INT(_debug, OID_AUTO, nlookup_debug, CTLFLAG_RW, &nlookup_debug, 0,
	"Force retry test");

static int naccess(struct nchandle *nch, int vmode, struct ucred *cred,
			int *stickyp, int nchislocked);

/*
 * unmount operations flag NLC_IGNBADDIR in order to allow the
 * umount to successfully issue a nlookup() on the path in order
 * to extract the mount point.  Allow certain errors through.
 */
static __inline
int
keeperror(struct nlookupdata *nd, int error)
{
	if (error) {
		if ((nd->nl_flags & NLC_IGNBADDIR) == 0 ||
		   (error != EIO && error != EBADRPC && error != ESTALE)) {
			return 1;
		}
	}
	return 0;
}

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
	    if (nd->nl_path[0] == '/') {
		    if ((flags & NLC_NLNCH_NOINIT) == 0) {
			    nd->nl_basench = &p->p_fd->fd_nrdir;
			    cache_copy(nd->nl_basench, &nd->nl_nch);
		    }
		    cache_copy(&p->p_fd->fd_nrdir, &nd->nl_rootnch);
		    if (p->p_fd->fd_njdir.ncp)
			cache_copy(&p->p_fd->fd_njdir, &nd->nl_jailnch);
		    nd->nl_cred = td->td_ucred;
		    nd->nl_flags |= NLC_BORROWCRED;
	    } else {
		    if ((flags & NLC_NLNCH_NOINIT) == 0) {
			    nd->nl_basench = &p->p_fd->fd_ncdir;
			    cache_copy(nd->nl_basench, &nd->nl_nch);
		    }
		    cache_copy(&p->p_fd->fd_nrdir, &nd->nl_rootnch);
		    if (p->p_fd->fd_njdir.ncp)
			cache_copy(&p->p_fd->fd_njdir, &nd->nl_jailnch);
		    nd->nl_cred = td->td_ucred;
		    nd->nl_flags |= NLC_BORROWCRED;
	    }
	} else {
	    if ((flags & NLC_NLNCH_NOINIT) == 0) {
		    nd->nl_basench = &rootnch;
		    cache_copy(nd->nl_basench, &nd->nl_nch);
	    }
	    cache_copy(&rootnch, &nd->nl_rootnch);
	    cache_copy(&rootnch, &nd->nl_jailnch);
	    nd->nl_cred = proc0.p_ucred;
	    nd->nl_flags |= NLC_BORROWCRED;
	}
	nd->nl_td = td;
	nd->nl_flags |= flags & ~NLC_NLNCH_NOINIT;
    } else {
	nlookup_done(nd);
    }
    return(error);
}


/*
 * nlookup_init() for "at" family of syscalls.
 *
 * Similar to nlookup_init() but if the path is relative and fd is not
 * AT_FDCWD, the path will be interpreted relative to the directory pointed
*  to by fd.  In this case, the file entry pointed to by fd is ref'ed and
*  returned in *fpp.
 *
 * If the call succeeds, nlookup_done_at() must be called to clean-up the nd
 * and release the ref to the file entry.
 */
int
nlookup_init_at(struct nlookupdata *nd, struct file **fpp, int fd, 
		const char *path, enum uio_seg seg, int flags)
{
	struct thread *td = curthread;
	struct file* fp;
	struct vnode *vp;
	int error;

	*fpp = NULL;

	/*
	 * Resolve the path, we might have to copy it in from userland,
	 * but don't initialize nl_basench, or nl_nch.
	 */
	error = nlookup_init(nd, path, seg, flags | NLC_NLNCH_NOINIT);
	if (__predict_false(error))
		return (error);

	/*
	 * Setup nl_basench (a pointer only not refd), and copy+ref
	 * to initialize nl_nch.  Only applicable to relative paths.
	 * For absolute paths, or if (fd) is degenerate, just use the
	 * normal path.
	 */
	if (nd->nl_path[0] == '/') {
		struct proc *p = curproc;
		nd->nl_basench = &p->p_fd->fd_nrdir;
	} else if (fd == AT_FDCWD) {
		struct proc *p = curproc;
		nd->nl_basench = &p->p_fd->fd_ncdir;
	} else {
		if ((error = holdvnode(td, fd, &fp)) != 0)
			goto done;
		vp = (struct vnode*)fp->f_data;
		if (vp->v_type != VDIR || fp->f_nchandle.ncp == NULL) {
			fdrop(fp);
			fp = NULL;
			error = ENOTDIR;
			goto done;
		}
		nd->nl_basench = &fp->f_nchandle;
		*fpp = fp;
	}
	cache_copy(nd->nl_basench, &nd->nl_nch);
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

#if 0
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
		if ((nd->nl_flags & NLC_BORROWCRED) == 0)
			crfree(nd->nl_cred);
		nd->nl_flags &= ~NLC_BORROWCRED;
		nd->nl_cred = cred;
	}
}
#endif

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
	if (nd->nl_flags & NLC_NCPISLOCKED)
	    cache_unlock(&nd->nl_nch);
	cache_drop_and_cache(&nd->nl_nch, nd->nl_elmno);
    }
    nd->nl_flags &= ~NLC_NCPISLOCKED;
    if (nd->nl_rootnch.ncp)
	cache_drop_and_cache(&nd->nl_rootnch, 0);
    if (nd->nl_jailnch.ncp)
	cache_drop_and_cache(&nd->nl_jailnch, 0);
    if ((nd->nl_flags & NLC_HASBUF) && nd->nl_path) {
	objcache_put(namei_oc, nd->nl_path);
	nd->nl_path = NULL;
    }
    if (nd->nl_cred) {
	if ((nd->nl_flags & NLC_BORROWCRED) == 0)
	    crfree(nd->nl_cred);
	nd->nl_cred = NULL;
	nd->nl_flags &= ~NLC_BORROWCRED;
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
    nd->nl_basench = NULL;
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
 * O_CREAT or O_TRUNC need the last element to be locked exlcusively.
 * Intermediate elements are always locked shared.
 *
 * NOTE: Even if we return on-zero, an unresolved namecache record
 *	 will always be locked exclusively.
 */
static __inline
int
wantsexcllock(struct nlookupdata *nd, int last_element)
{
	if ((nd->nl_flags & NLC_SHAREDLOCK) == 0)
		return(last_element);
	return 0;
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
    struct nchandle nctmp;
    struct mount *mp;
    int wasdotordotdot;
    char *path_reset;
    char *ptr;
    char *nptr;
    int error;
    int len;
    int dflags;
    int hit = 1;
    int saveflag = nd->nl_flags;
    boolean_t doretry = FALSE;
    boolean_t inretry = FALSE;

    if (nlookup_debug > 0) {
	--nlookup_debug;
	doretry = 1;
    }
    path_reset = NULL;

nlookup_start:

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
	int last_element;

	++nd->nl_elmno;
	KKASSERT((nd->nl_flags & NLC_NCPISLOCKED) == 0);

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

	    /*
	     * We might already be at the root as a pre-optimization
	     */
	    if (nd->nl_nch.mount != nd->nl_rootnch.mount ||
		nd->nl_nch.ncp != nd->nl_rootnch.ncp) {
		cache_drop_and_cache(&nd->nl_nch, 0);
		cache_copy(&nd->nl_rootnch, &nd->nl_nch);
	    }

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
		nd->nl_flags |= NLC_NCPISLOCKED;
		cache_lock_maybe_shared(&nd->nl_nch,
					wantsexcllock(nd, islastelement(ptr)));
		break;
	    }
	    continue;
	}

	/*
	 * Pre-calculate next path component so we can check whether the
	 * current component directory is the last directory in the path
	 * or not.
	 */
	for (nptr = ptr; *nptr && *nptr != '/'; ++nptr)
		;

	/*
	 * nd->nl_nch is referenced and not locked here.
	 *
	 * Check directory search permissions.  This will load dflags to
	 * obtain directory-special permissions to be checked along with the
	 * last component.
	 *
	 * We only need to pass-in &dflags for the second-to-last component.
	 * Optimize by passing-in NULL for any prior components, which may
	 * allow the code to bypass the naccess() call.
	 *
	 * naccess() is optimized to avoid having to lock the nch or get
	 * the related vnode if cached perms are sufficient.
	 */
	dflags = 0;
	if (*nptr == '/' || (saveflag & NLC_MODIFYING_MASK) == 0)
	    error = naccess(&nd->nl_nch, NLC_EXEC, nd->nl_cred, NULL, 0);
	else
	    error = naccess(&nd->nl_nch, NLC_EXEC, nd->nl_cred, &dflags, 0);
	if (error) {
	    if (keeperror(nd, error))
		    break;
	    error = 0;
	}

	/*
	 * Extract the next (or last) path component.  Path components are
	 * limited to 255 characters.
	 */
	nlc.nlc_nameptr = ptr;
	nlc.nlc_namelen = nptr - ptr;
	ptr = nptr;
	if (nlc.nlc_namelen >= 256) {
	    error = ENAMETOOLONG;
	    break;
	}
	last_element = islastelement(nptr);

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
	 * This subsection returns a referenced and possibly locked 'nch'.
	 * The locking status is based on the last_element flag.
	 *
	 * The namecache topology is not allowed to be disconnected, so 
	 * encountering a NULL parent will generate EINVAL.  This typically
	 * occurs when a directory is removed out from under a process.
	 *
	 * WARNING! The unlocking of nd->nl_nch is sensitive code.
	 */
	KKASSERT((nd->nl_flags & NLC_NCPISLOCKED) == 0);

	if (nlc.nlc_namelen == 1 && nlc.nlc_nameptr[0] == '.') {
	    if (last_element) {
		cache_get_maybe_shared(&nd->nl_nch, &nch,
				       wantsexcllock(nd, 1));
	    } else {
		cache_copy(&nd->nl_nch, &nch);
	    }
	    wasdotordotdot = 1;
	} else if (nlc.nlc_namelen == 2 && 
		   nlc.nlc_nameptr[0] == '.' && nlc.nlc_nameptr[1] == '.') {
	    if (nd->nl_nch.mount == nd->nl_rootnch.mount &&
		nd->nl_nch.ncp == nd->nl_rootnch.ncp
	    ) {
		/*
		 * ".." at the root returns the root
		 */
		if (last_element) {
		    cache_get_maybe_shared(&nd->nl_nch, &nch,
					   wantsexcllock(nd, 1));
		} else {
		    cache_copy(&nd->nl_nch, &nch);
		}
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
		if (last_element) {
		    cache_get_maybe_shared(&nctmp, &nch,
					   wantsexcllock(nd, 1));
		} else {
		    cache_copy(&nctmp, &nch);
		}
	    }
	    wasdotordotdot = 2;
	} else {
	    /*
	     * Quickly lookup the component.  If we can't find it, then
	     * slowly lookup and resolve the component.
	     */
	    if (last_element) {
		    error = cache_nlookup_maybe_shared(&nd->nl_nch, &nlc,
						       wantsexcllock(nd, 1),
						       &nch);
	    } else {
		    nch = cache_nlookup_nonlocked(&nd->nl_nch, &nlc);
		    if (nch.ncp == NULL)
			error = EWOULDBLOCK;
	    }

	    /*
	     * At this point the only possible error is EWOULDBLOCK.
	     *
	     * If no error nch is set and referenced, and then also locked
	     * according to last_element.  For EWOULDBLOCK nch is not set.
	     * For any other error nch is set and referenced, but not locked.
	     *
	     * On EWOULDBLOCK the ncp may be unresolved (if not locked it can
	     * become unresolved at any time, but we don't care at this time).
	     */
	    if (error == EWOULDBLOCK) {
		nch = cache_nlookup(&nd->nl_nch, &nlc);
		if (nch.ncp->nc_flag & NCF_UNRESOLVED)
		    hit = 0;
		for (;;) {
		    error = cache_resolve(&nch, nd->nl_cred);
		    if (error != EAGAIN &&
			(nch.ncp->nc_flag & NCF_DESTROYED) == 0) {
			    if (error == ESTALE) {
				if (!inretry)
				    error = ENOENT;
				doretry = TRUE;
			    }
			    if (last_element == 0)
				    cache_unlock(&nch);
			    break;
		    }
		    kprintf("[diagnostic] nlookup: relookup %*.*s\n",
			    nch.ncp->nc_nlen, nch.ncp->nc_nlen,
			    nch.ncp->nc_name);
		    cache_put(&nch);
		    nch = cache_nlookup(&nd->nl_nch, &nlc);
		}
	    }
	    wasdotordotdot = 0;
	}

	/*
	 * If the component is "." or ".." our dflags no longer represents
	 * the parent directory and we have to explicitly look it up.
	 *
	 * Expect the parent to be good since nch is locked.
	 *
	 * nch will continue to be valid even if an error occurs after this
	 * point.
	 */
	if (wasdotordotdot && error == 0) {
	    struct nchandle par;

	    dflags = 0;
	    if (last_element == 0)
		cache_lock_maybe_shared(&nch, wantsexcllock(nd, 0));

	    if ((par.ncp = nch.ncp->nc_parent) != NULL) {
		par.mount = nch.mount;
		cache_hold(&par);
		error = naccess(&par, 0, nd->nl_cred, &dflags, 0);
		cache_drop_and_cache(&par, nd->nl_elmno - 1);
		if (error) {
		    if (!keeperror(nd, error))
			error = 0;
		    if (error == EINVAL) {
			kprintf("nlookup (%s): trailing . or .. retry on %s\n",
				curthread->td_comm, nd->nl_path);
			doretry = TRUE;
		    }
		}
	    }

	    if (last_element == 0)
		cache_unlock(&nch);
	}

	/*
	 * [end of subsection]
	 *
	 * nch is referenced and locked according to (last_element).
	 * nd->nl_nch is unlocked and referenced.
	 */
	KKASSERT((nd->nl_flags & NLC_NCPISLOCKED) == 0);

	/*
	 * Resolve the namespace if necessary.  The ncp returned by
	 * cache_nlookup() is referenced, and also locked according
	 * to last_element.
	 *
	 * XXX neither '.' nor '..' should return EAGAIN since they were
	 * previously resolved and thus cannot be newly created ncp's.
	 */
	if (nch.ncp->nc_flag & NCF_UNRESOLVED) {
	    if (last_element == 0)
		cache_lock(&nch);
	    hit = 0;
	    error = cache_resolve(&nch, nd->nl_cred);
	    if (error == ESTALE) {
		if (!inretry)
		    error = ENOENT;
		doretry = TRUE;
	    }
	    if (last_element == 0)
		cache_unlock(&nch);
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
	if (last_element) {
	    if (error == ENOENT &&
		(nd->nl_flags & (NLC_CREATE | NLC_RENAME_DST))
	    ) {
		if (nd->nl_flags & NLC_NFS_RDONLY) {
			error = EROFS;
		} else {
			error = naccess(&nch, nd->nl_flags | dflags,
					nd->nl_cred, NULL, last_element);
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
	    if (last_element)
		    cache_unlock(&nch);
	    cache_drop_and_cache(&nch, nd->nl_elmno);
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
		if (last_element)
			cache_unlock(&nch);
		cache_drop_and_cache(&nch, nd->nl_elmno);
		break;
	    }
	    if (last_element == 0)
		cache_lock_maybe_shared(&nch, 1);

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

	    if (path_reset) {
		if (nd->nl_flags & NLC_HASBUF)
		    objcache_put(namei_oc, nd->nl_path);
	    } else {
		path_reset = nd->nl_path;
	    }
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
	    if (last_element)
		    cache_unlock(&nch);
	    cache_drop_and_cache(&nch, nd->nl_elmno);

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

	    /*
	     * We don't need to lock the nch unless the entry is unresolved
	     * or this is the last element.
	     */
	    if (last_element)
		cache_get_maybe_shared(&mp->mnt_ncmountpt, &nch,
				       wantsexcllock(nd, 1));
	    else
		cache_copy(&mp->mnt_ncmountpt, &nch);

	    if (nch.ncp->nc_flag & NCF_UNRESOLVED) {
		if (last_element == 0)
		    cache_lock(&nch);
		if (nch.ncp->nc_flag & NCF_UNRESOLVED) {
		    if (vfs_do_busy == 0) {
			vfs_do_busy = 1;
			if (last_element == 0)
			    cache_unlock(&nch);
			goto again;
		    }
		    error = VFS_ROOT(mp, &tdp);
		    vfs_unbusy(mp);
		    vfs_do_busy = 0;
		    if (keeperror(nd, error)) {
			cache_dropmount(mp);
			if (last_element == 0)
			    cache_unlock(&nch);
			break;
		    }
		    if (error == 0) {
			cache_setvp(&nch, tdp);
			vput(tdp);
		    }
		}
		if (last_element == 0)
		    cache_unlock(&nch);
	    }
	    if (vfs_do_busy)
		vfs_unbusy(mp);
	    cache_dropmount(mp);
	}

	/*
	 * Break out on error
	 */
	if (keeperror(nd, error)) {
	    if (last_element)
		    cache_unlock(&nch);
	    cache_drop_and_cache(&nch, nd->nl_elmno);
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
	    cache_drop_and_cache(&nd->nl_nch, nd->nl_elmno);
	    if (last_element)
		    cache_unlock(&nch);
	    /*nchislocked = 0; not needed */
	    KKASSERT((nd->nl_flags & NLC_NCPISLOCKED) == 0);
	    nd->nl_nch = nch;
	    continue;
	}

	/*
	 * Failure case: additional elements and the current element
	 * is not a directory
	 */
	if (*ptr) {
	    if (last_element)
		    cache_unlock(&nch);
	    cache_drop_and_cache(&nch, nd->nl_elmno);
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
	KKASSERT(last_element);

	if (nch.ncp->nc_vp && (nd->nl_flags & NLC_ALLCHKS)) {
	    error = naccess(&nch, nd->nl_flags | dflags,
			    nd->nl_cred, NULL, 1);
	    if (keeperror(nd, error)) {
		cache_put(&nch);
		break;
	    }
	}

	/*
	 * Termination: no more elements.
	 *
	 * Check to see if the immediate parent has been destroyed.  This race
	 * can occur because the element lookup must temporarily unlock
	 * the parent.  If so, do a retry.
	 */
	if (nch.ncp->nc_parent &&
	    (nch.ncp->nc_parent->nc_flag & NCF_DESTROYED)) {
		doretry = TRUE;
	}

	/*
	 * Termination: no more elements.
	 *
	 * If NLC_REFDVP is set acquire a referenced parent dvp.  Typically
	 * used for mkdir/mknod/ncreate/nremove/unlink/rename.
	 *
	 * NOTE: nd->nl_nch does not necessarily represent the parent
	 *	 directory, e.g. due to a mount point transition.
	 *
	 * nch is locked, standard lock order for the namecache is
	 * child-to-parent so we can safely lock its parent.  We can
	 * just use cache_dvpref().
	 *
	 * If nc_parent is NULL this is probably a mount point or a deleted
	 * file and there is no legal parent directory.  However, we do not
	 * want to fail the nlookup() because a higher level may wish to
	 * return a better error code, such as mkdir("/mntpt") would want to
	 * return EEXIST
	 */
	if ((nd->nl_flags & NLC_REFDVP) &&
	    (doretry == FALSE || inretry == TRUE)) {
		if (nch.ncp->nc_parent) {
			nd->nl_dvp = cache_dvpref(nch.ncp);
			if (nd->nl_dvp == NULL) {
				error = EINVAL;
				if (keeperror(nd, error)) {
					kprintf("Parent directory lost during "
						"nlookup: %s/%s (%08x/%08x)\n",
						nch.ncp->nc_parent->nc_name,
						nch.ncp->nc_name,
						nch.ncp->nc_parent->nc_flag,
						nch.ncp->nc_flag);
					cache_put(&nch);
					break;
				}
			}
		} else {
			error = 0;
			cache_put(&nch);
			break;
		}
	}
	cache_drop_and_cache(&nd->nl_nch, nd->nl_elmno);
	nd->nl_nch = nch;
	nd->nl_flags |= NLC_NCPISLOCKED;
	error = 0;
	break;
    }

    /*
     * We are done / or possibly retry
     */

    if (hit)
	++gd->gd_nchstats->ncs_longhits;
    else
	++gd->gd_nchstats->ncs_longmiss;

    if (nd->nl_flags & NLC_NCPISLOCKED)
	KKASSERT(cache_lockstatus(&nd->nl_nch) > 0);

    /*
     * Reset nd->nl_path if necessary (due to softlinks).  We want to return
     * nl_path to its original state before retrying or returning.
     */
    if (path_reset) {
	if (nd->nl_flags & NLC_HASBUF) {
	    objcache_put(namei_oc, nd->nl_path);
	    nd->nl_flags &= ~NLC_HASBUF;
	}
	nd->nl_path = path_reset;
	nd->nl_flags |= saveflag & NLC_HASBUF;
	path_reset = NULL;
    }

    /*
     * Retry the whole thing if doretry flag is set, but only once.
     *
     * autofs(5) may mount another filesystem under its root directory
     * while resolving a path.
     *
     * NFS might return ESTALE
     */
    if (doretry && !inretry) {
	kprintf("nlookup: errno %d retry %s\n", error, nd->nl_path);
	inretry = TRUE;

	/*
	 * Clean up nd->nl_nch and reset to base directory
	 */
	if (nd->nl_flags & NLC_NCPISLOCKED) {
		cache_unlock(&nd->nl_nch);
		nd->nl_flags &= ~NLC_NCPISLOCKED;
	}
	cache_drop(&nd->nl_nch);
	cache_copy(nd->nl_basench, &nd->nl_nch);

	nd->nl_elmno = 0;
	nd->nl_flags |= saveflag;

	goto nlookup_start;
    }

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

#define S_WXOK_MASK	(S_IRUSR|S_IXUSR|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH)
#define S_XOK_MASK	(S_IXUSR|S_IXGRP|S_IXOTH)

static int
naccess(struct nchandle *nch, int nflags, struct ucred *cred, int *nflagsp,
	int nchislocked)
{
    struct vnode *vp;
    struct vattr_lite lva;
    struct namecache *ncp;
    int error;
    int cflags;

    KKASSERT(nchislocked == 0 || cache_lockstatus(nch) > 0);

    ncp = nch->ncp;
again:
    if (ncp->nc_flag & NCF_UNRESOLVED) {
	if (nchislocked == 0) {
		cache_lock(nch);
		nchislocked = 2;
	}
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

	    if (nchislocked == 0) {
		cache_lock_maybe_shared(nch, 0);
		nchislocked = 2;
		if (ncp->nc_flag & NCF_UNRESOLVED)
			goto again;
	    }
	    if ((par.ncp = ncp->nc_parent) == NULL) {
		if (error != EAGAIN)
			error = EINVAL;
	    } else if (error == 0 || error == ENOENT) {
		par.mount = nch->mount;
		cache_hold(&par);
		cache_lock_maybe_shared(&par, 0);
		error = naccess(&par, NLC_WRITE, cred, NULL, 1);
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
     * Try to short-cut the vnode operation for intermediate directory
     * components.  This is a major SMP win because it avoids having
     * to execute a lot of code for intermediate directory components,
     * including shared refs and locks on intermediate directory vnodes.
     *
     * We can only do this if the caller does not need nflagsp.
     */
    if (error == 0 && nflagsp == NULL &&
	nflags == NLC_EXEC && (ncp->nc_flag & NCF_WXOK)) {
	if (nchislocked == 2)
		cache_unlock(nch);
	return 0;
    }

    /*
     * Get the vnode attributes so we can do the rest of our checks.
     *
     * NOTE: We only call naccess_lva() if the target exists.
     */
    if (error == 0) {
	if (nchislocked == 0) {
	    cache_lock_maybe_shared(nch, 0);
	    nchislocked = 2;
	}
#if 0
	error = cache_vget(nch, cred, LK_SHARED, &vp);
#else
	error = cache_vref(nch, cred, &vp);
#endif
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
	    error = VOP_GETATTR_LITE(vp, &lva);
	    if (error == 0 && (nflags & NLC_TRUNCATE)) {
		switch(lva.va_type) {
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
		switch(lva.va_type) {
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
#if 0
	    vput(vp);
#else
	    vrele(vp);
#endif

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
		    if ((lva.va_mode & VSVTX) && lva.va_uid != cred->cr_uid)
			*nflagsp |= NLC_STICKY;
		    if (lva.va_flags & APPEND)
			*nflagsp |= NLC_APPENDONLY;
		    if (lva.va_flags & IMMUTABLE)
			*nflagsp |= NLC_IMMUTABLE;
		}

		/*
		 * NCF_WXOK can be set for world-searchable directories.
		 *
		 * XXX When we implement capabilities this code would also
		 * need a cap check, or only set the flag if there are no
		 * capabilities.
		 */
		cflags = 0;
		if (lva.va_type == VDIR &&
		    (lva.va_mode & S_WXOK_MASK) == S_WXOK_MASK) {
			cflags |= NCF_WXOK;
		}
		if ((lva.va_mode & S_XOK_MASK) == 0)
			cflags |= NCF_NOTX;

		/*
		 * Track swapcache management flags in the namecache.
		 *
		 * Calculate the flags based on the current vattr_lite info
		 * and recalculate the inherited flags from the parent
		 * (the original cache linkage may have occurred without
		 * getattrs and thus have stale flags).
		 */
		if (lva.va_flags & SF_NOCACHE)
			cflags |= NCF_SF_NOCACHE;
		if (lva.va_flags & UF_CACHE)
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
		 * We're not supposed to update nc_flag when holding a shared
		 * lock, but we allow the case for certain flags.  Note that
		 * holding an exclusive lock allows updating nc_flag without
		 * atomics.  nc_flag is not allowe to be updated at all unless
		 * a shared or exclusive lock is held.
		 */
		atomic_clear_short(&ncp->nc_flag,
				   (NCF_SF_NOCACHE | NCF_UF_CACHE |
				   NCF_SF_PNOCACHE | NCF_UF_PCACHE |
				   NCF_WXOK | NCF_NOTX) & ~cflags);
		atomic_set_short(&ncp->nc_flag, cflags);

		/*
		 * Process general access.
		 */
		error = naccess_lva(&lva, nflags, cred);
	    }
	}
    }
    if (nchislocked == 2)
	cache_unlock(nch);
    return(error);
}

/*
 * Check the requested access against the given vattr using cred.
 */
int
naccess_lva(struct vattr_lite *lvap, int nflags, struct ucred *cred)
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
    if ((lvap->va_flags & IMMUTABLE) || (nflags & NLC_IMMUTABLE)) {
	if ((nflags & NLC_IMMUTABLE) && (nflags & NLC_HLINK))
	    return (EPERM);
	if (nflags & (NLC_CREATE | NLC_DELETE |
		      NLC_RENAME_SRC | NLC_RENAME_DST)) {
	    return (EPERM);
	}
	if (nflags & (NLC_WRITE | NLC_TRUNCATE)) {
	    switch(lvap->va_type) {
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
    if (((lvap->va_flags & NOUNLINK) || (nflags & NLC_APPENDONLY)) &&
	(nflags & (NLC_DELETE | NLC_RENAME_SRC | NLC_RENAME_DST))
    ) {
	return (EPERM);
    }

    /*
     * A file marked append-only may not be deleted but can be renamed.
     */
    if ((lvap->va_flags & APPEND) &&
	(nflags & (NLC_DELETE | NLC_RENAME_DST))
    ) {
	return (EPERM);
    }

    /*
     * A file marked append-only which is opened for writing must also
     * be opened O_APPEND.
     */
    if ((lvap->va_flags & APPEND) && (nflags & (NLC_OPEN | NLC_TRUNCATE))) {
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

    if (cred->cr_uid == lvap->va_uid) {
	if ((nflags & NLC_OWN) == 0) {
	    if ((vmode & lvap->va_mode) != vmode)
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
	if (lvap->va_gid == cred->cr_groups[i]) {
	    if ((vmode & lvap->va_mode) != vmode)
		return(EACCES);
	    return(0);
	}
    }

    /*
     * Check world perms
     */
    vmode >>= 3;
    if ((vmode & lvap->va_mode) != vmode)
	return(EACCES);
    return(0);
}

/*
 * Long-term (10-second interval) statistics collection
 */
static
uint64_t
collect_nlookup_callback(int n)
{
	static uint64_t last_total;
	uint64_t save;
	uint64_t total;

	total = 0;
	for (n = 0; n < ncpus; ++n) {
		globaldata_t gd = globaldata_find(n);
		struct nchstats *sp;

		if ((sp = gd->gd_nchstats) != NULL)
			total += sp->ncs_longhits + sp->ncs_longmiss;
	}
	save = total;
	total = total - last_total;
	last_total = save;

	return total;
}

static
void
nlookup_collect_init(void *dummy __unused)
{
	kcollect_register(KCOLLECT_NLOOKUP, "nlookup", collect_nlookup_callback,
			  KCOLLECT_SCALE(KCOLLECT_NLOOKUP_FORMAT, 0));
}
SYSINIT(collect_nlookup, SI_SUB_PROP, SI_ORDER_ANY, nlookup_collect_init, 0);
