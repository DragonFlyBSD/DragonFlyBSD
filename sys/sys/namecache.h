/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com>,
 *	All rights reserved.
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
 * $DragonFly: src/sys/sys/namecache.h,v 1.3 2004/04/08 17:56:46 dillon Exp $
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
};

typedef struct namecache *namecache_t;

/*
 * Flags in namecache.nc_flag (u_char)
 */
#define NCF_NEGATIVE	0x01	/* negative entry */
#define NCF_WHITEOUT	0x02	/* negative entry corresponds to whiteout */
#define NCF_UNRESOLVED	0x04	/* invalid or unresolved entry */
#define NCF_MOUNTPT	0x08	/* mount point */
#define NCF_ROOT	0x10	/* namecache root (static) */
#define NCF_HASHED	0x20	/* namecache entry in hash table */

#define CINV_SELF	0x0001	/* invalidate a specific (dvp,vp) entry */
#define CINV_CHILDREN	0x0002	/* invalidate all children of vp */

#define NCPNULL		((struct namecache *)NULL)	/* placemarker */
#define NCPPNULL	((struct namecache **)NULL)	/* placemarker */

#ifdef _KERNEL

struct vop_lookup_args;
struct componentname;
struct mount;

int	cache_lookup(struct vnode *dvp, struct namecache *par,
			struct vnode **vpp, struct namecache **ncpp,
			struct componentname *cnp);
void	cache_mount(struct vnode *dvp, struct vnode *tvp);
void	cache_enter(struct vnode *dvp, struct namecache *par,
			struct vnode *vp, struct componentname *cnp);
void	vfs_cache_setroot(struct vnode *vp);
void	cache_purge(struct vnode *vp);
void	cache_purgevfs (struct mount *mp);
void	cache_drop(struct namecache *ncp);
struct namecache *cache_hold(struct namecache *ncp);
int	cache_leaf_test (struct vnode *vp);
int	vfs_cache_lookup(struct vop_lookup_args *ap);
int	textvp_fullpath (struct proc *p, char **retbuf, char **retfreebuf);

#endif

#endif
