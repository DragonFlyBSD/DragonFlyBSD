/*
 * Copyright (c) 2011,2012 François Tigeot <ftigeot@wolpond.org>
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
#include <inttypes.h>

#include <sys/sysproto.h>
#include <libprop/proplib.h>
#include <libprop/prop_dictionary.h>

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
	KASSERT(res == NULL, ("unode_insert(): RB_INSERT didn't return NULL"));

	return unp;
}

struct ac_gnode*
gnode_insert(struct mount *mp, gid_t gid)
{
	struct ac_gnode *gnp, *res;

	gnp = kmalloc(sizeof(struct ac_gnode), M_MOUNT, M_ZERO | M_WAITOK);

	gnp->left_bits = (gid >> ACCT_CHUNK_BITS);
	res = RB_INSERT(ac_gtree, &mp->mnt_acct.ac_groot, gnp);
	KASSERT(res == NULL, ("gnode_insert(): RB_INSERT didn't return NULL"));

	return gnp;
}

int vfs_quota_enabled = 0;
TUNABLE_INT("vfs.quota_enabled", &vfs_quota_enabled);
SYSCTL_INT(_vfs, OID_AUTO, quota_enabled, CTLFLAG_RD,
                 &vfs_quota_enabled, 0, "Enable VFS quota");

/* initializes per mount-point data structures */
void
vq_init(struct mount *mp)
{

	if (!vfs_quota_enabled)
		return;

	/* initialize the rb trees */
	RB_INIT(&mp->mnt_acct.ac_uroot);
	RB_INIT(&mp->mnt_acct.ac_groot);
	spin_init(&mp->mnt_acct.ac_spin, "vqinit");

	mp->mnt_acct.ac_bytes = 0;

	/* enable data collection */
	mp->mnt_op->vfs_account = vfs_stdaccount;
	/* mark this filesystem quota enabled */
	mp->mnt_flag |= MNT_QUOTA;
	if (bootverbose)
		kprintf("vfs accounting enabled for %s\n",
		    mp->mnt_stat.f_mntonname);
}


void
vq_done(struct mount *mp)
{
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
	unp->uid_chunk[(uid & ACCT_CHUNK_MASK)].space += delta;
	gnp->gid_chunk[(gid & ACCT_CHUNK_MASK)].space += delta;

	spin_unlock(&mp->mnt_acct.ac_spin);
}

static void
cmd_get_usage_all(struct mount *mp, prop_array_t dict_out)
{
	struct ac_unode *unp;
	struct ac_gnode *gnp;
	int i;
	prop_dictionary_t item;

	item = prop_dictionary_create();
	(void) prop_dictionary_set_uint64(item, "space used", mp->mnt_acct.ac_bytes);
	(void) prop_dictionary_set_uint64(item, "limit", mp->mnt_acct.ac_limit);
	prop_array_add_and_rel(dict_out, item);

	RB_FOREACH(unp, ac_utree, &mp->mnt_acct.ac_uroot) {
		for (i=0; i<ACCT_CHUNK_NIDS; i++) {
			if (unp->uid_chunk[i].space != 0) {
				item = prop_dictionary_create();
				(void) prop_dictionary_set_uint32(item, "uid",
					(unp->left_bits << ACCT_CHUNK_BITS) + i);
				(void) prop_dictionary_set_uint64(item, "space used",
					unp->uid_chunk[i].space);
				(void) prop_dictionary_set_uint64(item, "limit",
					unp->uid_chunk[i].limit);
				prop_array_add_and_rel(dict_out, item);
			}
		}
	}

	RB_FOREACH(gnp, ac_gtree, &mp->mnt_acct.ac_groot) {
		for (i=0; i<ACCT_CHUNK_NIDS; i++) {
			if (gnp->gid_chunk[i].space != 0) {
				item = prop_dictionary_create();
				(void) prop_dictionary_set_uint32(item, "gid",
					(gnp->left_bits << ACCT_CHUNK_BITS) + i);
				(void) prop_dictionary_set_uint64(item, "space used",
					gnp->gid_chunk[i].space);
				(void) prop_dictionary_set_uint64(item, "limit",
					gnp->gid_chunk[i].limit);
				prop_array_add_and_rel(dict_out, item);
			}
		}
	}
}

static int
cmd_set_usage_all(struct mount *mp, prop_array_t args)
{
	struct ac_unode ufind, *unp;
	struct ac_gnode gfind, *gnp;
	prop_dictionary_t item;
	prop_object_iterator_t iter;
	uint32_t id;
	uint64_t space;

	spin_lock(&mp->mnt_acct.ac_spin);
	/* 0. zero all statistics */
	/* we don't bother to free up memory, most of it would probably be
	 * re-allocated immediately anyway. just bzeroing the existing nodes
	 * is fine */
	mp->mnt_acct.ac_bytes = 0;
	RB_FOREACH(unp, ac_utree, &mp->mnt_acct.ac_uroot) {
		bzero(&unp->uid_chunk, sizeof(unp->uid_chunk));
	}
	RB_FOREACH(gnp, ac_gtree, &mp->mnt_acct.ac_groot) {
		bzero(&gnp->gid_chunk, sizeof(gnp->gid_chunk));
	}

	/* args contains an array of dict */
	iter = prop_array_iterator(args);
	if (iter == NULL) {
		kprintf("cmd_set_usage_all(): failed to create iterator\n");
		spin_unlock(&mp->mnt_acct.ac_spin);
		return 1;
	}
	while ((item = prop_object_iterator_next(iter)) != NULL) {
		prop_dictionary_get_uint64(item, "space used", &space);
		if (prop_dictionary_get_uint32(item, "uid", &id)) {
			ufind.left_bits = (id >> ACCT_CHUNK_BITS);
			unp = RB_FIND(ac_utree, &mp->mnt_acct.ac_uroot, &ufind);
			if (unp == NULL)
				unp = unode_insert(mp, id);
			unp->uid_chunk[(id & ACCT_CHUNK_MASK)].space = space;
		} else if (prop_dictionary_get_uint32(item, "gid", &id)) {
			gfind.left_bits = (id >> ACCT_CHUNK_BITS);
			gnp = RB_FIND(ac_gtree, &mp->mnt_acct.ac_groot, &gfind);
			if (gnp == NULL)
				gnp = gnode_insert(mp, id);
			gnp->gid_chunk[(id & ACCT_CHUNK_MASK)].space = space;
		} else {
			mp->mnt_acct.ac_bytes = space;
		}
	}
	prop_object_iterator_release(iter);

	spin_unlock(&mp->mnt_acct.ac_spin);
	return 0;
}

static int
cmd_set_limit(struct mount *mp, prop_dictionary_t args)
{
	uint64_t limit;

	prop_dictionary_get_uint64(args, "limit", &limit);

	spin_lock(&mp->mnt_acct.ac_spin);
	mp->mnt_acct.ac_limit = limit;
	spin_unlock(&mp->mnt_acct.ac_spin);

	return 0;
}

static int
cmd_set_limit_uid(struct mount *mp, prop_dictionary_t args)
{
	uint64_t limit;
	uid_t uid;
	struct ac_unode ufind, *unp;

	prop_dictionary_get_uint32(args, "uid", &uid);
	prop_dictionary_get_uint64(args, "limit", &limit);

	ufind.left_bits = (uid >> ACCT_CHUNK_BITS);

	spin_lock(&mp->mnt_acct.ac_spin);
	if ((unp = RB_FIND(ac_utree, &mp->mnt_acct.ac_uroot, &ufind)) == NULL)
		unp = unode_insert(mp, uid);
	unp->uid_chunk[(uid & ACCT_CHUNK_MASK)].limit = limit;
	spin_unlock(&mp->mnt_acct.ac_spin);

	return 0;
}

static int
cmd_set_limit_gid(struct mount *mp, prop_dictionary_t args)
{
	uint64_t limit;
	gid_t gid;
	struct ac_gnode gfind, *gnp;

	prop_dictionary_get_uint32(args, "gid", &gid);
	prop_dictionary_get_uint64(args, "limit", &limit);

	gfind.left_bits = (gid >> ACCT_CHUNK_BITS);

	spin_lock(&mp->mnt_acct.ac_spin);
	if ((gnp = RB_FIND(ac_gtree, &mp->mnt_acct.ac_groot, &gfind)) == NULL)
		gnp = gnode_insert(mp, gid);
	gnp->gid_chunk[(gid & ACCT_CHUNK_MASK)].limit = limit;
	spin_unlock(&mp->mnt_acct.ac_spin);

	return 0;
}

int
sys_vquotactl(struct vquotactl_args *vqa)
/* const char *path, struct plistref *pref */
{
	const char *path;
	struct plistref pref;
	prop_dictionary_t dict;
	prop_object_t args;
	char *cmd;

	prop_array_t pa_out;

	struct nlookupdata nd;
	struct mount *mp;
	int error;

	if (!vfs_quota_enabled)
		return EOPNOTSUPP;
	path = vqa->path;
	error = copyin(vqa->pref, &pref, sizeof(pref));
	error = prop_dictionary_copyin(&pref, &dict);
	if (error != 0)
		return(error);

	/* we have a path, get its mount point */
	error = nlookup_init(&nd, path, UIO_USERSPACE, 0);
	if (error != 0)
		return (error);
	error = nlookup(&nd);
	if (error != 0)
		return (error);
	mp = nd.nl_nch.mount;
	nlookup_done(&nd);

	/* get the command */
	if (prop_dictionary_get_cstring(dict, "command", &cmd) == 0) {
		kprintf("sys_vquotactl(): couldn't get command\n");
		return EINVAL;
	}
	args = prop_dictionary_get(dict, "arguments");
	if (args == NULL) {
		kprintf("couldn't get arguments\n");
		return EINVAL;
	}

	pa_out = prop_array_create();
	if (pa_out == NULL)
		return ENOMEM;

	if (strcmp(cmd, "get usage all") == 0) {
		cmd_get_usage_all(mp, pa_out);
		goto done;
	}
	if (strcmp(cmd, "set usage all") == 0) {
		error = cmd_set_usage_all(mp, args);
		goto done;
	}
	if (strcmp(cmd, "set limit") == 0) {
		error = cmd_set_limit(mp, args);
		goto done;
	}
	if (strcmp(cmd, "set limit uid") == 0) {
		error = cmd_set_limit_uid(mp, args);
		goto done;
	}
	if (strcmp(cmd, "set limit gid") == 0) {
		error = cmd_set_limit_gid(mp, args);
		goto done;
	}
	return EINVAL;

done:
	/* kernel to userland */
	dict = prop_dictionary_create();
	error = prop_dictionary_set(dict, "returned data", pa_out);

	error = prop_dictionary_copyout(&pref, dict);
	error = copyout(&pref, vqa->pref, sizeof(pref));

	return error;
}

/*
 * Returns a valid mount point for accounting purposes
 * We cannot simply use vp->v_mount if the vnode belongs
 * to a PFS mount point
 */
struct mount*
vq_vptomp(struct vnode *vp)
{
	/* XXX: vp->v_pfsmp may point to a freed structure
	* we use mountlist_exists() to check if it is valid
	* before using it */
	if ((vp->v_pfsmp != NULL) && (mountlist_exists(vp->v_pfsmp))) {
		/* This is a PFS, use a copy of the real mp */
		return vp->v_pfsmp;
	} else {
		/* Not a PFS or a PFS beeing unmounted */
		return vp->v_mount;
	}
}

int
vq_write_ok(struct mount *mp, uid_t uid, gid_t gid, uint64_t delta)
{
	int rv = 1;
	struct ac_unode ufind, *unp;
	struct ac_gnode gfind, *gnp;
	uint64_t space, limit;

	spin_lock(&mp->mnt_acct.ac_spin);

	if (mp->mnt_acct.ac_limit == 0)
		goto check_uid;
	if ((mp->mnt_acct.ac_bytes + delta) > mp->mnt_acct.ac_limit) {
		rv = 0;
		goto done;
	}

check_uid:
	ufind.left_bits = (uid >> ACCT_CHUNK_BITS);
	if ((unp = RB_FIND(ac_utree, &mp->mnt_acct.ac_uroot, &ufind)) == NULL) {
		space = 0;
		limit = 0;
	} else {
		space = unp->uid_chunk[(uid & ACCT_CHUNK_MASK)].space;
		limit = unp->uid_chunk[(uid & ACCT_CHUNK_MASK)].limit;
	}
	if (limit == 0)
		goto check_gid;
	if ((space + delta) > limit) {
		rv = 0;
		goto done;
	}

check_gid:
	gfind.left_bits = (gid >> ACCT_CHUNK_BITS);
	if ((gnp = RB_FIND(ac_gtree, &mp->mnt_acct.ac_groot, &gfind)) == NULL) {
		space = 0;
		limit = 0;
	} else {
		space = gnp->gid_chunk[(gid & ACCT_CHUNK_MASK)].space;
		limit = gnp->gid_chunk[(gid & ACCT_CHUNK_MASK)].limit;
	}
	if (limit == 0)
		goto done;
	if ((space + delta) > limit)
		rv = 0;

done:
	spin_unlock(&mp->mnt_acct.ac_spin);
	return rv;
}
