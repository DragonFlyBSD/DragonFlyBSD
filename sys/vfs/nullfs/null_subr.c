/*
 * Copyright (c) 1992, 1993
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
 *	@(#)null_subr.c	8.7 (Berkeley) 5/14/95
 *
 * $FreeBSD: src/sys/miscfs/nullfs/null_subr.c,v 1.21.2.4 2001/06/26 04:20:09 bp Exp $
 * $DragonFly: src/sys/vfs/nullfs/Attic/null_subr.c,v 1.13 2004/08/28 19:02:23 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/malloc.h>
#include "null.h"

#define LOG2_SIZEVNODE 7		/* log2(sizeof struct vnode) */

/*
 * Null layer cache:
 * Each cache entry holds a reference to the lower vnode
 * along with a pointer to the alias vnode.  When an
 * entry is added the lower vnode is vref'd.  When the
 * alias is removed the lower vnode is vrele'd.
 */

#define	NULL_NHASH(vp) \
	(&null_node_hashtbl[(((uintptr_t)vp)>>LOG2_SIZEVNODE) & null_node_hash])

static struct null_node **null_node_hashtbl;
static u_long null_node_hash;
static struct lwkt_token null_ihash_token;

static MALLOC_DEFINE(M_NULLFSHASH, "NULLFS hash", "NULLFS hash table");
MALLOC_DEFINE(M_NULLFSNODE, "NULLFS node", "NULLFS vnode private part");

static int	null_node_alloc(struct mount *mp, struct vnode *lowervp,
				     struct vnode **vpp);
static struct vnode *
		null_node_find(struct mount *mp, struct vnode *lowervp);

/*
 * Initialise cache headers
 */
int
nullfs_init(struct vfsconf *vfsp)
{
	NULLFSDEBUG("nullfs_init\n");		/* printed during system boot */
	null_node_hash = 16;
	while (null_node_hash < desiredvnodes)
		null_node_hash <<= 1;
	null_node_hashtbl = malloc(sizeof(void *) * null_node_hash,
				    M_NULLFSHASH, M_WAITOK|M_ZERO);
	--null_node_hash;
	lwkt_token_init(&null_ihash_token);
	return (0);
}

int
nullfs_uninit(struct vfsconf *vfsp)
{
        if (null_node_hashtbl) {
		free(null_node_hashtbl, M_NULLFSHASH);
		null_node_hashtbl = NULL;
	}
	return (0);
}

/*
 * Return a vref'ed alias for lower vnode if already exists, else 0.
 * Lower vnode should be locked (but with no additional refs) on entry
 * and will be unlocked on return if the search was successful, and left
 * locked if the search was not successful.
 */
static struct vnode *
null_node_find(struct mount *mp, struct vnode *lowervp)
{
	struct thread *td = curthread;	/* XXX */
	struct null_node *np;
	struct null_node *xp;
	struct vnode *vp;
	lwkt_tokref ilock;

	lwkt_gettoken(&ilock, &null_ihash_token);
loop:
	for (np = *NULL_NHASH(lowervp); np; np = np->null_next) {
		if (np->null_lowervp == lowervp && NULLTOV(np)->v_mount == mp) {
			vp = NULLTOV(np);
			if (vget(vp, NULL, LK_EXCLUSIVE | LK_CANRECURSE, td)) {
				printf ("null_node_find: vget failed.\n");
				goto loop;
			}

			/*
			 * vget() might have blocked, we have to check that
			 * our vnode is still valid.
			 */
			xp = *NULL_NHASH(lowervp);
			while (xp) {
				if (xp == np && xp->null_lowervp == lowervp &&
				    NULLTOV(xp) == vp &&
				    NULLTOV(xp)->v_mount == mp) {
					break;
				}
				xp = xp->null_next;
			}
			if (xp == NULL) {
				printf ("null_node_find: node race, retry.\n");
				vput(vp);
				goto loop;
			}
			/*
			 * SUCCESS!  Returned the locked and referenced vp
			 * and release the lock on lowervp.
			 */
			VOP_UNLOCK(lowervp, NULL, 0, td);
			lwkt_reltoken(&ilock);
			return (vp);
		}
	}

	/*
	 * Failure, leave lowervp locked on return.
	 */
	lwkt_reltoken(&ilock);
	return(NULL);
}

int
null_node_add(struct null_node *np)
{
	struct null_node **npp;
	struct null_node *n2;
	lwkt_tokref ilock;

	lwkt_gettoken(&ilock, &null_ihash_token);
	npp = NULL_NHASH(np->null_lowervp);
	while ((n2 = *npp) != NULL) {
		if (n2->null_lowervp == np->null_lowervp) {
			lwkt_reltoken(&ilock);
			return(EBUSY);
		}
		npp = &n2->null_next;
	}
	np->null_next = NULL;
	*npp = np;
	lwkt_reltoken(&ilock);
	return(0);
}

void
null_node_rem(struct null_node *np)
{
	struct null_node **npp;
	struct null_node *n2;
	lwkt_tokref ilock;

	lwkt_gettoken(&ilock, &null_ihash_token);
	npp = NULL_NHASH(np->null_lowervp);
	while ((n2 = *npp) != NULL) {
		if (n2 == np)
			break;
		npp = &n2->null_next;
	}
	KKASSERT(np == n2);
	*npp = np->null_next;
	np->null_next = NULL;
	lwkt_reltoken(&ilock);
}

/*
 * Make a new null_node node.  vp is the null mount vnode, lowervp is the
 * lower vnode.  Maintain a reference to (lowervp).  lowervp must be
 * locked on call.
 */
static int
null_node_alloc(struct mount *mp, struct vnode *lowervp, struct vnode **vpp)
{
	struct null_node *np;
	struct thread *td;
	struct vnode *vp;
	int error;

	td = curthread;
retry:
	/*
	 * If we have already hashed the vp we can just return it.
	 */
	*vpp = null_node_find(mp, lowervp);
	if (*vpp)
		return 0;

	/*
	 * lowervp is locked but not referenced at this point.
	 */
	MALLOC(np, struct null_node *, sizeof(struct null_node),
	       M_NULLFSNODE, M_WAITOK);

	error = getnewvnode(VT_NULL, mp, mp->mnt_vn_ops, vpp, 0, LK_CANRECURSE);
	if (error) {
		FREE(np, M_NULLFSNODE);
		return (error);
	}
	vp = *vpp;

	/*
	 * Set up the np/vp relationship and set the lower vnode.
	 *
	 * XXX:
	 * When nullfs encounters sockets or device nodes, it
	 * has a hard time working with the normal vp union, probably
	 * because the device has not yet been opened.  Needs investigation.
	 */
	vp->v_type = lowervp->v_type;
	if (vp->v_type == VCHR || vp->v_type == VBLK)
		addaliasu(vp, lowervp->v_udev);
	else
		vp->v_un = lowervp->v_un;	/* XXX why this assignment? */
	np->null_vnode = vp;
	np->null_lowervp = lowervp;

	/*
	 * Lock our new vnode
	 */
	error = VOP_LOCK(vp, NULL, LK_EXCLUSIVE | LK_THISLAYER, td);
	if (error)
		panic("null_node_alloc: can't lock new vnode\n");

	/*
	 * Try to add our new node to the hash table.  If a collision
	 * occurs someone else beat us to it and we need to destroy the
	 * vnode and retry.
	 */
	if (null_node_add(np) != 0) {
		free(np, M_NULLFSNODE);
		vput(vp);
		goto retry;
	}

	/*
	 * Finish up.  Link the vnode and null_node together, ref lowervp
	 * for the null node.  lowervp is already locked so the lock state
	 * is already properly synchronized.
	 */
	vp->v_data = np;
	vref(lowervp);
	return (0);
}


/*
 * Try to find an existing null_node vnode refering to the given underlying
 * vnode (which should be locked and referenced). If no vnode found, create
 * a new null_node vnode which contains a reference to the lower vnode.
 */
int
null_node_create(struct mount *mp, struct vnode *lowervp, struct vnode **newvpp)
{
	struct vnode *aliasvp;

	aliasvp = null_node_find(mp, lowervp);
	if (aliasvp) {
		/*
		 * null_node_find() has unlocked lowervp for us, so we just
		 * have to get rid of the reference.
		 */
		vrele(lowervp);
#ifdef NULLFS_DEBUG
		vprint("null_node_create: exists", aliasvp);
#endif
	} else {
		int error;

		/*
		 * Get new vnode.  Note that lowervp is locked and referenced
		 * at this point (as it was passed to us).
		 */
		NULLFSDEBUG("null_node_create: create new alias vnode\n");

		/*
		 * Make new vnode reference the null_node.
		 */
		error = null_node_alloc(mp, lowervp, &aliasvp);
		if (error)
			return error;

		/*
		 * aliasvp is already locked and ref'd by getnewvnode()
		 */
	}

#ifdef DIAGNOSTIC
	if (lowervp->v_usecount < 1) {
		/* Should never happen... */
		vprint ("null_node_create: alias ", aliasvp);
		vprint ("null_node_create: lower ", lowervp);
		panic ("null_node_create: lower has 0 usecount.");
	};
#endif

#ifdef NULLFS_DEBUG
	vprint("null_node_create: alias", aliasvp);
	vprint("null_node_create: lower", lowervp);
#endif

	*newvpp = aliasvp;
	return (0);
}

#ifdef DIAGNOSTIC
#include "opt_ddb.h"

#ifdef DDB
#define	null_checkvp_barrier	1
#else
#define	null_checkvp_barrier	0
#endif

struct vnode *
null_checkvp(struct vnode *vp, char *fil, int lno)
{
	struct null_node *a = VTONULL(vp);
	if (a->null_lowervp == NULLVP) {
		/* Should never happen */
		int i; u_long *p;
		printf("vp = %p, ZERO ptr\n", (void *)vp);
		for (p = (u_long *) a, i = 0; i < 8; i++)
			printf(" %lx", p[i]);
		printf("\n");
		/* wait for debugger */
		while (null_checkvp_barrier) /*WAIT*/ ;
		panic("null_checkvp");
	}
	if (a->null_lowervp->v_usecount < 1) {
		int i; u_long *p;
		printf("vp = %p, unref'ed lowervp\n", (void *)vp);
		for (p = (u_long *) a, i = 0; i < 8; i++)
			printf(" %lx", p[i]);
		printf("\n");
		/* wait for debugger */
		while (null_checkvp_barrier) /*WAIT*/ ;
		panic ("null with unref'ed lowervp");
	};
#ifdef notyet
	printf("null %x/%d -> %x/%d [%s, %d]\n",
	        NULLTOV(a), NULLTOV(a)->v_usecount,
		a->null_lowervp, a->null_lowervp->v_usecount,
		fil, lno);
#endif
	return a->null_lowervp;
}
#endif
