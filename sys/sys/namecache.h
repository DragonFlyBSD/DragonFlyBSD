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
 * $DragonFly: src/sys/sys/namecache.h,v 1.16 2004/11/18 20:04:26 dillon Exp $
 */

#ifndef _SYS_NAMECACHE_H_
#define	_SYS_NAMECACHE_H_

struct vnode;

TAILQ_HEAD(namecache_list, namecache);

/*
 * The namecache structure is used to manage the filesystem namespace.  Most
 * vnodes cached by the system will reference one or more associated namecache
 * structures.
 *
 * The namecache is disjoint, there may not always be a path to the system
 * root through nc_parent links.  If a namecache entry has no parent, that
 * entry will not be hashed and can only be 'found' via '.' or '..'.
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
 * Many new API VOP operations do not pass vnodes.  In these cases the
 * operations vector is typically obtained via nc_mount->mnt_vn_ops.
 */
struct namecache {
    LIST_ENTRY(namecache) nc_hash;	/* hash chain (nc_parent,name) */
    TAILQ_ENTRY(namecache) nc_entry;	/* scan via nc_parent->nc_list */
    TAILQ_ENTRY(namecache) nc_vnode;	/* scan via vnode->v_namecache */
    struct namecache_list  nc_list;	/* list of children */
    struct namecache *nc_parent;	/* namecache entry for parent */
    struct	vnode *nc_vp;		/* vnode representing name or NULL */
    int		nc_refs;		/* ref count prevents deletion */
    u_short	nc_flag;
    u_char	nc_nlen;		/* The length of the name, 255 max */
    u_char	nc_unused;
    char	*nc_name;		/* Separately allocated seg name */
    int		nc_error;
    int		nc_timeout;		/* compared against ticks, or 0 */
    int		nc_exlocks;		/* namespace locking */
    struct thread *nc_locktd;		/* namespace locking */
    struct mount *nc_mount;		/* associated mount for vopops */
};

typedef struct namecache *namecache_t;

/*
 * Flags in namecache.nc_flag (u_char)
 */
#define NCF_LOCKED	0x0001	/* locked namespace */
#define NCF_WHITEOUT	0x0002	/* negative entry corresponds to whiteout */
#define NCF_UNRESOLVED	0x0004	/* invalid or unresolved entry */
#define NCF_MOUNTPT	0x0008	/* mount point */
#define NCF_ROOT	0x0010	/* namecache root (static) */
#define NCF_HASHED	0x0020	/* namecache entry in hash table */
#define NCF_LOCKREQ	0x0040
#define NCF_UNUSED080	0x0080
#define NCF_ISSYMLINK	0x0100	/* represents a symlink */
#define NCF_ISDIR	0x0200	/* represents a directory */
#define NCF_DESTROYED	0x0400	/* name association is considered destroyed */

/*
 * cache_inval[_vp]() flags
 */
#define CINV_DESTROY	0x0001	/* flag so cache_nlookup ignores the ncp */
#define CINV_UNUSED02	0x0002
#define CINV_CHILDREN	0x0004	/* recursively set children to unresolved */

#ifdef _KERNEL

struct vop_lookup_args;
struct componentname;
struct nlcomponent;
struct mount;

void	cache_lock(struct namecache *ncp);
int	cache_lock_nonblock(struct namecache *ncp);
void	cache_unlock(struct namecache *ncp);
void	cache_setvp(struct namecache *ncp, struct vnode *vp);
void	cache_settimeout(struct namecache *ncp, int nticks);
void	cache_setunresolved(struct namecache *ncp);
struct namecache *cache_nlookup(struct namecache *par, struct nlcomponent *nlc);
struct namecache *cache_allocroot(struct mount *mp, struct vnode *vp);
void	cache_inval(struct namecache *ncp, int flags);
void	cache_inval_vp(struct vnode *vp, int flags);
void	vfs_cache_setroot(struct vnode *vp, struct namecache *ncp);

int	cache_resolve(struct namecache *ncp, struct ucred *cred);
void	cache_purge(struct vnode *vp);
void	cache_purgevfs (struct mount *mp);
int	cache_get_nonblock(struct namecache *ncp);
void	cache_cleanneg(int count);
struct namecache *cache_get(struct namecache *ncp);
struct namecache *cache_hold(struct namecache *ncp);
void	cache_put(struct namecache *ncp);
void	cache_drop(struct namecache *ncp);
void	cache_rename(struct namecache *fncp, struct namecache *tncp);
int	cache_vget(struct namecache *, struct ucred *, int, struct vnode **);
int	cache_vref(struct namecache *, struct ucred *, struct vnode **);
struct namecache *cache_fromdvp(struct vnode *, struct ucred *, int);

#endif

#endif
