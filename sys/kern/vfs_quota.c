/*
 * Copyright (c) 2011 François Tigeot <ftigeot@wolpond.org>
 * All rights reserved.
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

#include <sys/sysctl.h>
#include <sys/mount.h>
#include <sys/systm.h>
#include <sys/nlookup.h>
#include <sys/vnode.h>
#include <sys/stat.h>
#include <sys/vfs_quota.h>
#include <sys/spinlock.h>
#include <sys/spinlock2.h>

/* in-memory accounting, red-black tree based */

/* FIXME: code duplication caused by uid_t / gid_t differences */
RB_PROTOTYPE(ac_utree, ac_unode, rb_entry, rb_ac_unode_cmp);
RB_PROTOTYPE(ac_gtree, ac_gnode, rb_entry, rb_ac_gnode_cmp);

static int
rb_ac_unode_cmp(struct ac_unode *a, struct ac_unode *b);
static int
rb_ac_gnode_cmp(struct ac_gnode *a, struct ac_gnode *b);

RB_GENERATE(ac_utree, ac_unode, rb_entry, rb_ac_unode_cmp);
RB_GENERATE(ac_gtree, ac_gnode, rb_entry, rb_ac_gnode_cmp);

struct ac_unode* unode_insert(struct mount*, uid_t);
struct ac_gnode* gnode_insert(struct mount*, gid_t);

static int
rb_ac_unode_cmp(struct ac_unode *a, struct ac_unode *b)
{
	if (a->left_bits < b->left_bits)
		return(-1);
	else if (a->left_bits > b->left_bits)
		return(1);
	return(0);
}

static int
rb_ac_gnode_cmp(struct ac_gnode *a, struct ac_gnode *b)
{
	if (a->left_bits < b->left_bits)
		return(-1);
	else if (a->left_bits > b->left_bits)
		return(1);
	return(0);
}

struct ac_unode*
unode_insert(struct mount *mp, uid_t uid)
{
	struct ac_unode *unp, *res;

	unp = kmalloc(sizeof(struct ac_unode), M_MOUNT, M_ZERO | M_WAITOK);

	unp->left_bits = (uid >> ACCT_CHUNK_BITS);
	res = RB_INSERT(ac_utree, &mp->mnt_acct.ac_uroot, unp);
	KASSERT(res == NULL, ("unode_insert(): RB_INSERT didn't return NULL\n"));

	return unp;
}

struct ac_gnode*
gnode_insert(struct mount *mp, gid_t gid)
{
	struct ac_gnode *gnp, *res;

	gnp = kmalloc(sizeof(struct ac_gnode), M_MOUNT, M_ZERO | M_WAITOK);

	gnp->left_bits = (gid >> ACCT_CHUNK_BITS);
	res = RB_INSERT(ac_gtree, &mp->mnt_acct.ac_groot, gnp);
	KASSERT(res == NULL, ("gnode_insert(): RB_INSERT didn't return NULL\n"));

	return gnp;
}

/* initializes global accounting data */
void
vq_init(struct mount *mp) {

	/* initialize the rb trees */
	RB_INIT(&mp->mnt_acct.ac_uroot);
	RB_INIT(&mp->mnt_acct.ac_groot);
	spin_init(&mp->mnt_acct.ac_spin);

	mp->mnt_acct.ac_bytes = 0;

	/* enable data collection */
	mp->mnt_op->vfs_account = vfs_stdaccount;
	/* mark this filesystem as having accounting enabled */
	mp->mnt_flag |= MNT_ACCOUNTING;
	if (bootverbose)
		kprintf("vfs accounting enabled for %s\n",
		    mp->mnt_stat.f_mntonname);
}


void
vq_done(struct mount *mp) {
	/* TODO: remove the rb trees here */
}

void
vfs_stdaccount(struct mount *mp, uid_t uid, gid_t gid, int64_t delta)
{
	struct ac_unode ufind, *unp;
	struct ac_gnode gfind, *gnp;

	/* find or create address of chunk */
	ufind.left_bits = (uid >> ACCT_CHUNK_BITS);
	gfind.left_bits = (gid >> ACCT_CHUNK_BITS);

	spin_lock(&mp->mnt_acct.ac_spin);

	mp->mnt_acct.ac_bytes += delta;

	if ((unp = RB_FIND(ac_utree, &mp->mnt_acct.ac_uroot, &ufind)) == NULL)
		unp = unode_insert(mp, uid);
	if ((gnp = RB_FIND(ac_gtree, &mp->mnt_acct.ac_groot, &gfind)) == NULL)
		gnp = gnode_insert(mp, gid);

	/* update existing chunk */
	unp->uid_chunk[(uid & ACCT_CHUNK_MASK)] += delta;
	gnp->gid_chunk[(gid & ACCT_CHUNK_MASK)] += delta;

	spin_unlock(&mp->mnt_acct.ac_spin);
}
