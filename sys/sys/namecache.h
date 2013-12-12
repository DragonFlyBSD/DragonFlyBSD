/*
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
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
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $DragonFly: src/sys/sys/namecache.h,v 1.31 2008/05/09 17:52:18 dillon Exp $
 */

#ifndef _SYS_NAMECACHE_H_
#define	_SYS_NAMECACHE_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _SYS_SPINLOCK_H_
#include <sys/spinlock.h>
#endif
#ifndef _SYS_THREAD_H_
#include <sys/thread.h>
#endif

struct vnode;
struct ucred;
struct proc;

TAILQ_HEAD(namecache_list, namecache);

/*
 * The namecache structure is used to manage the filesystem namespace.  Most
 * vnodes cached by the system will reference one or more associated namecache
 * structures.
 *
 * The DragonFly namecache maintains elements from active nodes to the root
 * in all but the NFS server case and the removed file/directory case.
 * NFS servers use fhtovp() and may have to regenerate the topology to
 * the leaf on the fly.
 *
 * Because the namecache structure maintains the path through mount points,
 * null, and union mounts, and other VFS overlays, several namecache
 * structures may pass through the same vnode.  Also note that namespaces
 * relating to non-existant (i.e. not-yet-created) files/directories may be
 * locked.  Lock coherency is achieved by requiring that the particular
 * namecache record whos parent represents the physical directory in which
 * the namespace operation is to occur be the one that is locked.  In 
 * overlay cases, the (union, nullfs) VFS, or in namei when crossing a mount
 * point, may have to obtain multiple namespace record locks to avoid
 * confusion, but only the one representing the physical directory is passed
 * into lower layer VOP calls.
 *
 * ncp locking is done using atomic ops on nc_lockstatus, including a request
 * flag for waiters.  nc_locktd is set after locking or cleared before
 * the last unlock.  ncp locks are reentrant.
 *
 * Many new API VOP operations do not pass vnodes.  In these cases the
 * operations vector is typically obtained via nc_mount->mnt_vn_use_ops.
 */
struct namecache {
    LIST_ENTRY(namecache) nc_hash;	/* hash chain (nc_parent,name) */
    TAILQ_ENTRY(namecache) nc_entry;	/* scan via nc_parent->nc_list */
    TAILQ_ENTRY(namecache) nc_vnode;	/* scan via vnode->v_namecache */
    struct namecache_list  nc_list;	/* list of children */
    struct nchash_head	*nc_head;
    struct namecache	*nc_parent;	/* namecache entry for parent */
    struct vnode	*nc_vp;		/* vnode representing name or NULL */
    int			nc_refs;	/* ref count prevents deletion */
    u_short		nc_flag;
    u_char		nc_nlen;	/* The length of the name, 255 max */
    u_char		nc_unused;
    char		*nc_name;	/* Separately allocated seg name */
    int			nc_error;
    int			nc_timeout;	/* compared against ticks, or 0 */
    u_int		nc_lockstatus;	/* namespace locking */
    struct thread	*nc_locktd;	/* namespace locking */
    long		nc_namecache_gen; /* cmp against mnt_namecache_gen */
};

/*
 * Namecache handles include a mount reference allowing topologies
 * to be replicated with mount overlays (nullfs mounts).
 */
struct nchandle {
    struct namecache *ncp;		/* ncp in underlying filesystem */
    struct mount *mount;		/* mount pt (possible overlay) */
};

/*
 * Flags in namecache.nc_flag (u_char)
 */
#define NCF_UNUSED01	0x0001
#define NCF_WHITEOUT	0x0002	/* negative entry corresponds to whiteout */
#define NCF_UNRESOLVED	0x0004	/* invalid or unresolved entry */
#define NCF_ISMOUNTPT	0x0008	/* someone may have mounted on us here */
#define NCF_SF_NOCACHE	0x0010	/* track swapcache chflags from attr */
#define NCF_UF_CACHE	0x0020
#define NCF_SF_PNOCACHE	0x0040	/* track from parent */
#define NCF_UF_PCACHE	0x0080
#define NCF_ISSYMLINK	0x0100	/* represents a symlink */
#define NCF_ISDIR	0x0200	/* represents a directory */
#define NCF_DESTROYED	0x0400	/* name association is considered destroyed */
#define NCF_DEFEREDZAP	0x0800	/* zap defered due to lock unavailability */

#define NC_EXLOCK_REQ	0x80000000	/* nc_lockstatus state flag */
#define NC_SHLOCK_REQ	0x40000000	/* nc_lockstatus state flag */
#define NC_SHLOCK_FLAG	0x20000000	/* nc_lockstatus state flag */
#define NC_SHLOCK_VHOLD	0x10000000	/* nc_lockstatus state flag */

/*
 * cache_inval[_vp]() flags
 */
#define CINV_DESTROY	0x0001	/* flag so cache_nlookup ignores the ncp */
#define CINV_UNUSED02	0x0002
#define CINV_CHILDREN	0x0004	/* recursively set children to unresolved */

#ifdef _KERNEL

struct componentname;
struct nlcomponent;
struct mount;

void	cache_lock(struct nchandle *nch);
void	cache_lock_maybe_shared(struct nchandle *nch, int excl);
void	cache_relock(struct nchandle *nch1, struct ucred *cred1,
			struct nchandle *nch2, struct ucred *cred2);
int	cache_lock_nonblock(struct nchandle *nch);
void	cache_unlock(struct nchandle *nch);
int	cache_lockstatus(struct nchandle *nch);
void	cache_setvp(struct nchandle *nch, struct vnode *vp);
void	cache_settimeout(struct nchandle *nch, int nticks);
void	cache_setunresolved(struct nchandle *nch);
void	cache_clrmountpt(struct nchandle *nch);
struct nchandle cache_nlookup(struct nchandle *nch, struct nlcomponent *nlc);
struct nchandle cache_nlookup_nonblock(struct nchandle *nch,
			struct nlcomponent *nlc);
int	cache_nlookup_maybe_shared(struct nchandle *nch,
			struct nlcomponent *nlc, int excl,
			struct nchandle *nchres);
void	cache_allocroot(struct nchandle *nch, struct mount *mp, struct vnode *vp);
struct mount *cache_findmount(struct nchandle *nch);
void	cache_dropmount(struct mount *mp);
void	cache_ismounting(struct mount *mp);
void	cache_unmounting(struct mount *mp);
int	cache_inval(struct nchandle *nch, int flags);
int	cache_inval_vp(struct vnode *vp, int flags);
int	cache_inval_vp_nonblock(struct vnode *vp);
void	vfs_cache_setroot(struct vnode *vp, struct nchandle *nch);

int	cache_resolve(struct nchandle *nch, struct ucred *cred);
void	cache_purge(struct vnode *vp);
void	cache_purgevfs (struct mount *mp);
void	cache_hysteresis(int critpath);
void	cache_get(struct nchandle *nch, struct nchandle *target);
int	cache_get_nonblock(struct nchandle *nch, struct nchandle *target);
void	cache_get_maybe_shared(struct nchandle *nch,
			struct nchandle *target, int excl);
struct nchandle *cache_hold(struct nchandle *nch);
void	cache_copy(struct nchandle *nch, struct nchandle *target);
void	cache_changemount(struct nchandle *nch, struct mount *mp);
void	cache_put(struct nchandle *nch);
void	cache_drop(struct nchandle *nch);
void	cache_zero(struct nchandle *nch);
void	cache_rename(struct nchandle *fnch, struct nchandle *tnch);
void	cache_unlink(struct nchandle *nch);
int	cache_isopen(struct nchandle *nch);
int	cache_vget(struct nchandle *, struct ucred *, int, struct vnode **);
int	cache_vref(struct nchandle *, struct ucred *, struct vnode **);
int	cache_fromdvp(struct vnode *, struct ucred *, int, struct nchandle *);
int	cache_fullpath(struct proc *, struct nchandle *, struct nchandle *,
			char **, char **, int);

#endif

#endif
