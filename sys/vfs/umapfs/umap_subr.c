/*
 * Copyright (c) 1992, 1993, 1995
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software donated to Berkeley by
 * Jan-Simon Pendry.
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
 *	@(#)umap_subr.c	8.9 (Berkeley) 5/14/95
 *
 * $FreeBSD: src/sys/miscfs/umapfs/umap_subr.c,v 1.19 1999/09/04 11:51:41 bde Exp $
 * $DragonFly: src/sys/vfs/umapfs/Attic/umap_subr.c,v 1.13 2004/12/17 00:18:46 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/malloc.h>
#include "umap.h"

#define LOG2_SIZEVNODE 7		/* log2(sizeof struct vnode) */

/*
 * Null layer cache:
 * Each cache entry holds a reference to the target vnode
 * along with a pointer to the alias vnode.  When an
 * entry is added the target vnode is vref'd.  When the
 * alias is removed the target vnode is vrele'd.
 */

#define	UMAP_NHASH(vp) \
	(&umap_node_hashtbl \
	[((uintptr_t)(void *)(vp) >> LOG2_SIZEVNODE) & umap_node_hash])
static struct umap_node **umap_node_hashtbl;
static u_long umap_node_hash;

static u_long	umap_findid (u_long id, u_long map[][2], int nentries);
static int	umap_node_alloc (struct mount *mp, struct vnode *lowervp,
				     struct vnode **vpp);
static struct vnode *
		umap_node_find (struct mount *mp, struct vnode *targetvp);

/*
 * Initialise cache headers
 */
int
umapfs_init(struct vfsconf *vfsp)
{
#ifdef DEBUG
	printf("umapfs_init\n");		/* printed during system boot */
#endif
	umap_node_hash = 16;
	while (umap_node_hash < desiredvnodes)
		umap_node_hash <<= 1;
	umap_node_hashtbl = malloc(sizeof(void *) * umap_node_hash, M_CACHE,
				   M_WAITOK|M_ZERO);
	--umap_node_hash;
	return (0);
}

/*
 * umap_findid is called by various routines in umap_vnodeops.c to
 * find a user or group id in a map.
 */
static u_long
umap_findid(u_long id, u_long map[][2], int nentries)
{
	int i;

	/* Find uid entry in map */
	i = 0;
	while ((i<nentries) && ((map[i][0]) != id))
		i++;

	if (i < nentries)
		return (map[i][1]);
	else
		return (-1);
}

/*
 * umap_reverse_findid is called by umap_getattr() in umap_vnodeops.c to
 * find a user or group id in a map, in reverse.
 */
u_long
umap_reverse_findid(u_long id, u_long map[][2], int nentries)
{
	int i;

	/* Find uid entry in map */
	i = 0;
	while ((i<nentries) && ((map[i][1]) != id))
		i++;

	if (i < nentries)
		return (map[i][0]);
	else
		return (-1);
}

/*
 * Return alias for target vnode if already exists, else 0.
 */
static struct vnode *
umap_node_find(struct mount *mp, struct vnode *targetvp)
{
	struct thread *td = curthread;		/* XXX */
	struct umap_node **xpp;
	struct umap_node *xp;
	struct vnode *vp;

#ifdef DEBUG
	printf("umap_node_find(mp = %p, target = %p)\n",
	    (void *)mp, (void *)targetvp);
#endif

	/*
	 * Find hash base, and then search the (two-way) linked
	 * list looking for a umap_node structure which is referencing
	 * the target vnode.  If found, the increment the umap_node
	 * reference count (but NOT the target vnode's vref counter).
	 */
	xpp = UMAP_NHASH(targetvp);
loop:
	for (xp = *xpp; xp; xp = (void *)xp->umap_next) {
		if (xp->umap_lowervp == targetvp &&
		    xp->umap_vnode->v_mount == mp) {
			vp = UMAPTOV(xp);
			/*
			 * We need vget for the VXLOCK
			 * stuff, but we don't want to lock
			 * the lower node.
			 */
			if (vget(vp, LK_EXCLUSIVE|LK_SLEEPFAIL, td)) {
#ifdef DEBUG
				printf ("umap_node_find: vget failed.\n");
#endif
				goto loop;
			}
			return (vp);
		}
	}

#ifdef DEBUG
	printf("umap_node_find(%p, %p): NOT found\n",
	    (void *)mp, (void *)targetvp);
#endif

	return (0);
}

/*
 * Make a new umap_node node.
 * Vp is the alias vnode, lofsvp is the target vnode.
 * Maintain a reference to (targetvp).
 */
static int
umap_node_alloc(struct mount *mp, struct vnode *lowervp, struct vnode **vpp)
{
	struct umap_node *xp;
	struct umap_node **xpp;
	struct vnode *othervp, *vp;
	int error;

	/* XXX This routine probably needs a node_alloc lock */

	/*
	 * Do the MALLOC before the getnewvnode since doing so afterward
	 * might cause a bogus v_data pointer to get dereferenced
	 * elsewhere if MALLOC should block.
	 */
	MALLOC(xp, struct umap_node *, sizeof(struct umap_node),
	    M_TEMP, M_WAITOK);

	error = getnewvnode(VT_UMAP, mp, vpp, 0, 0);
	if (error) {
		FREE(xp, M_TEMP);
		return (error);
	}
	vp = *vpp;

	vp->v_type = lowervp->v_type;
	xp->umap_vnode = vp;
	vp->v_data = xp;
	xp->umap_lowervp = lowervp;
	/*
	 * Before we insert our new node onto the hash chains,
	 * check to see if someone else has beaten us to it.
	 * (We could have slept in MALLOC.)
	 */
	othervp = umap_node_find(mp, lowervp);
	if (othervp) {
		FREE(xp, M_TEMP);
		vp->v_type = VBAD;	/* node is discarded */
		vx_put(vp);
		*vpp = othervp;
		return (0);
	}
	vref(lowervp);   /* Extra vref will be vrele'd in umap_node_create */
	xpp = UMAP_NHASH(lowervp);
	xp->umap_next = (void *)*xpp;
	*xpp = xp;
	return (0);
}

void
umap_node_delete(struct umap_node *xp)
{
	struct umap_node **xpp;

	xpp = UMAP_NHASH(xp->umap_lowervp);
	while (*xpp && *xpp != xp)
		xpp = &(*xpp)->umap_next;
	KKASSERT(*xpp);
	*xpp = (void *)xp->umap_next;
	xp->umap_next = NULL;
}

/*
 * Try to find an existing umap_node vnode refering
 * to it, otherwise make a new umap_node vnode which
 * contains a reference to the target vnode.
 */
int
umap_node_create(struct mount *mp, struct vnode *targetvp,
		 struct vnode **newvpp)
{
	struct vnode *aliasvp;

	aliasvp = umap_node_find(mp, targetvp);
	if (aliasvp) {
		/*
		 * Take another reference to the alias vnode
		 */
#ifdef DEBUG
		vprint("umap_node_create: exists", aliasvp);
#endif
		/* vref(aliasvp); */
	} else {
		int error;

		/*
		 * Get new vnode.
		 */
#ifdef DEBUG
		printf("umap_node_create: create new alias vnode\n");
#endif
		/*
		 * Make new vnode reference the umap_node.
		 */
		error = umap_node_alloc(mp, targetvp, &aliasvp);
		if (error)
			return (error);

		/*
		 * aliasvp is already vref'd by getnewvnode()
		 */
	}

	vrele(targetvp);

#ifdef DEBUG
	vprint("umap_node_create: alias", aliasvp);
	vprint("umap_node_create: target", targetvp);
#endif

	*newvpp = aliasvp;
	return (0);
}

#ifdef DIAGNOSTIC
int umap_checkvp_barrier = 1;
struct vnode *
umap_checkvp(struct vnode *vp, char *fil, int lno)
{
	struct umap_node *a = VTOUMAP(vp);
	if (a->umap_lowervp == NULL) {
		/* Should never happen */
		int i; u_long *p;
		printf("vp = %p, ZERO ptr\n", (void *)vp);
		for (p = (u_long *) a, i = 0; i < 8; i++)
			printf(" %p", (void *)p[i]);
		printf("\n");
		/* wait for debugger */
		while (umap_checkvp_barrier) /*WAIT*/ ;
		panic("umap_checkvp");
	}
	if (a->umap_lowervp->v_usecount < 1) {
		int i; u_long *p;
		printf("vp = %p, unref'ed lowervp\n", (void *)vp);
		for (p = (u_long *) a, i = 0; i < 8; i++)
			printf(" %p", (void *)p[i]);
		printf("\n");
		/* wait for debugger */
		while (umap_checkvp_barrier) /*WAIT*/ ;
		panic ("umap with unref'ed lowervp");
	}
#if 0
	printf("umap %x/%d -> %x/%d [%s, %d]\n",
	        a->umap_vnode, a->umap_vnode->v_usecount,
		a->umap_lowervp, a->umap_lowervp->v_usecount,
		fil, lno);
#endif
	return (a->umap_lowervp);
}
#endif /* DIAGNOSTIC */

/* umap_mapids maps all of the ids in a credential, both user and group. */

void
umap_mapids(struct mount *v_mount, struct ucred *credp)
{
	int i;
	uid_t uid;
	gid_t gid;

	if (credp == NOCRED)
		return;

	/* Find uid entry in map */

	uid = (uid_t) umap_findid(credp->cr_uid,
				MOUNTTOUMAPMOUNT(v_mount)->info_mapdata,
				MOUNTTOUMAPMOUNT(v_mount)->info_nentries);

	if (uid != -1)
		credp->cr_uid = uid;
	else
		credp->cr_uid = (uid_t) NOBODY;

#ifdef notdef
	/* cr_gid is the same as cr_groups[0] in 4BSD */

	/* Find gid entry in map */

	gid = (gid_t) umap_findid(credp->cr_gid,
				MOUNTTOUMAPMOUNT(v_mount)->info_gmapdata,
				MOUNTTOUMAPMOUNT(v_mount)->info_gnentries);

	if (gid != -1)
		credp->cr_gid = gid;
	else
		credp->cr_gid = NULLGROUP;
#endif

	/* Now we must map each of the set of groups in the cr_groups
		structure. */

	i = 0;
	while (credp->cr_groups[i] != 0) {
		gid = (gid_t) umap_findid(credp->cr_groups[i],
				MOUNTTOUMAPMOUNT(v_mount)->info_gmapdata,
				MOUNTTOUMAPMOUNT(v_mount)->info_gnentries);

		if (gid != -1)
			credp->cr_groups[i++] = gid;
		else
			credp->cr_groups[i++] = NULLGROUP;
	}
}
