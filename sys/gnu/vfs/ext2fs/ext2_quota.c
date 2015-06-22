/*
 * Copyright (c) 1982, 1986, 1990, 1993, 1995
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Robert Elz at The University of Melbourne.
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
 *	@(#)ufs_quota.c	8.5 (Berkeley) 5/20/95
 * $FreeBSD: src/sys/ufs/ufs/ufs_quota.c,v 1.27.2.3 2002/01/15 10:33:32 phk Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/fcntl.h>
#include <sys/proc.h>
#include <sys/nlookup.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <vm/vm_zone.h>

#include "quota.h"
#include "dinode.h"
#include "inode.h"
#include "ext2_mount.h"
#include "ext2_extern.h"

static MALLOC_DEFINE(M_EXT2DQUOT, "EXT2 quota", "EXT2 quota entries");

/*
 * Quota name to error message mapping.
 */
static char *quotatypes[] = INITQFNAMES;

static int ext2_chkdqchg (struct inode *, long, struct ucred *, int);
static int ext2_chkiqchg (struct inode *, long, struct ucred *, int);
static int ext2_dqget (struct vnode *,
		u_long, struct ext2_mount *, int, struct ext2_dquot **);
static int ext2_dqsync (struct vnode *, struct ext2_dquot *);
static void ext2_dqflush (struct vnode *);

#ifdef DIAGNOSTIC
static void ext2_dqref (struct ext2_dquot *);
static void ext2_chkdquot (struct inode *);
#endif

/*
 * Set up the quotas for an inode.
 *
 * This routine completely defines the semantics of quotas.
 * If other criterion want to be used to establish quotas, the
 * MAXQUOTAS value in quotas.h should be increased, and the
 * additional dquots set up here.
 */
int
ext2_getinoquota(struct inode *ip)
{
	struct ext2_mount *ump;
	struct vnode *vp = ITOV(ip);
	int error;

	ump = VFSTOEXT2(vp->v_mount);
	/*
	 * Set up the user quota based on file uid.
	 * EINVAL means that quotas are not enabled.
	 */
	if (ip->i_dquot[USRQUOTA] == NODQUOT &&
	    (error = ext2_dqget(vp, ip->i_uid, ump, USRQUOTA, &ip->i_dquot[USRQUOTA])) &&
	    error != EINVAL)
		return (error);
	/*
	 * Set up the group quota based on file gid.
	 * EINVAL means that quotas are not enabled.
	 */
	if (ip->i_dquot[GRPQUOTA] == NODQUOT &&
	    (error = ext2_dqget(vp, ip->i_gid, ump, GRPQUOTA, &ip->i_dquot[GRPQUOTA])) &&
	    error != EINVAL)
		return (error);
	return (0);
}

/*
 * Update disk usage, and take corrective action.
 */
int
ext2_chkdq(struct inode *ip, long change, struct ucred *cred, int flags)
{
	struct ext2_dquot *dq;
	int i;
	int ncurblocks, error;

#ifdef DIAGNOSTIC
	if ((flags & CHOWN) == 0)
		ext2_chkdquot(ip);
#endif
	if (change == 0)
		return (0);
	if (change < 0) {
		for (i = 0; i < MAXQUOTAS; i++) {
			if ((dq = ip->i_dquot[i]) == NODQUOT)
				continue;
			while (dq->dq_flags & DQ_LOCK) {
				dq->dq_flags |= DQ_WANT;
				(void) tsleep((caddr_t)dq, 0, "chkdq1", 0);
			}
			ncurblocks = dq->dq_curblocks + change;
			if (ncurblocks >= 0)
				dq->dq_curblocks = ncurblocks;
			else
				dq->dq_curblocks = 0;
			dq->dq_flags &= ~DQ_BLKS;
			dq->dq_flags |= DQ_MOD;
		}
		return (0);
	}
	if ((flags & FORCE) == 0 && cred->cr_uid != 0) {
		for (i = 0; i < MAXQUOTAS; i++) {
			if ((dq = ip->i_dquot[i]) == NODQUOT)
				continue;
			error = ext2_chkdqchg(ip, change, cred, i);
			if (error)
				return (error);
		}
	}
	for (i = 0; i < MAXQUOTAS; i++) {
		if ((dq = ip->i_dquot[i]) == NODQUOT)
			continue;
		while (dq->dq_flags & DQ_LOCK) {
			dq->dq_flags |= DQ_WANT;
			(void) tsleep((caddr_t)dq, 0, "chkdq2", 0);
		}
		/* Reset timer when crossing soft limit */
		if (dq->dq_curblocks + change >= dq->dq_bsoftlimit &&
		    dq->dq_curblocks < dq->dq_bsoftlimit)
			dq->dq_btime = time_second +
			    VFSTOEXT2(ITOV(ip)->v_mount)->um_btime[i];
		dq->dq_curblocks += change;
		dq->dq_flags |= DQ_MOD;
	}
	return (0);
}

/*
 * Check for a valid change to a users allocation.
 * Issue an error message if appropriate.
 */
static int
ext2_chkdqchg(struct inode *ip, long change, struct ucred *cred, int type)
{
	struct ext2_dquot *dq = ip->i_dquot[type];
	long ncurblocks = dq->dq_curblocks + change;

	/*
	 * If user would exceed their hard limit, disallow space allocation.
	 */
	if (ncurblocks >= dq->dq_bhardlimit && dq->dq_bhardlimit) {
		if ((dq->dq_flags & DQ_BLKS) == 0 &&
		    ip->i_uid == cred->cr_uid) {
			uprintf("\n%s: write failed, %s disk limit reached\n",
			    ITOV(ip)->v_mount->mnt_stat.f_mntfromname,
			    quotatypes[type]);
			dq->dq_flags |= DQ_BLKS;
		}
		return (EDQUOT);
	}
	/*
	 * If user is over their soft limit for too long, disallow space
	 * allocation. Reset time limit as they cross their soft limit.
	 */
	if (ncurblocks >= dq->dq_bsoftlimit && dq->dq_bsoftlimit) {
		if (dq->dq_curblocks < dq->dq_bsoftlimit) {
			dq->dq_btime = time_second +
			    VFSTOEXT2(ITOV(ip)->v_mount)->um_btime[type];
			if (ip->i_uid == cred->cr_uid)
				uprintf("\n%s: warning, %s %s\n",
				    ITOV(ip)->v_mount->mnt_stat.f_mntfromname,
				    quotatypes[type], "disk quota exceeded");
			return (0);
		}
		if (time_second > dq->dq_btime) {
			if ((dq->dq_flags & DQ_BLKS) == 0 &&
			    ip->i_uid == cred->cr_uid) {
				uprintf("\n%s: write failed, %s %s\n",
				    ITOV(ip)->v_mount->mnt_stat.f_mntfromname,
				    quotatypes[type],
				    "disk quota exceeded for too long");
				dq->dq_flags |= DQ_BLKS;
			}
			return (EDQUOT);
		}
	}
	return (0);
}

/*
 * Check the inode limit, applying corrective action.
 */
int
ext2_chkiq(struct inode *ip, long change, struct ucred *cred, int flags)
{
	struct ext2_dquot *dq;
	int i;
	int ncurinodes, error;

#ifdef DIAGNOSTIC
	if ((flags & CHOWN) == 0)
		ext2_chkdquot(ip);
#endif
	if (change == 0)
		return (0);
	if (change < 0) {
		for (i = 0; i < MAXQUOTAS; i++) {
			if ((dq = ip->i_dquot[i]) == NODQUOT)
				continue;
			while (dq->dq_flags & DQ_LOCK) {
				dq->dq_flags |= DQ_WANT;
				(void) tsleep((caddr_t)dq, 0, "chkiq1", 0);
			}
			ncurinodes = dq->dq_curinodes + change;
			if (ncurinodes >= 0)
				dq->dq_curinodes = ncurinodes;
			else
				dq->dq_curinodes = 0;
			dq->dq_flags &= ~DQ_INODS;
			dq->dq_flags |= DQ_MOD;
		}
		return (0);
	}
	if ((flags & FORCE) == 0 && cred->cr_uid != 0) {
		for (i = 0; i < MAXQUOTAS; i++) {
			if ((dq = ip->i_dquot[i]) == NODQUOT)
				continue;
			error = ext2_chkiqchg(ip, change, cred, i);
			if (error)
				return (error);
		}
	}
	for (i = 0; i < MAXQUOTAS; i++) {
		if ((dq = ip->i_dquot[i]) == NODQUOT)
			continue;
		while (dq->dq_flags & DQ_LOCK) {
			dq->dq_flags |= DQ_WANT;
			(void) tsleep((caddr_t)dq, 0, "chkiq2", 0);
		}
		/* Reset timer when crossing soft limit */
		if (dq->dq_curinodes + change >= dq->dq_isoftlimit &&
		    dq->dq_curinodes < dq->dq_isoftlimit)
			dq->dq_itime = time_second +
			    VFSTOEXT2(ITOV(ip)->v_mount)->um_itime[i];
		dq->dq_curinodes += change;
		dq->dq_flags |= DQ_MOD;
	}
	return (0);
}

/*
 * Check for a valid change to a users allocation.
 * Issue an error message if appropriate.
 */
static int
ext2_chkiqchg(struct inode *ip, long change, struct ucred *cred, int type)
{
	struct ext2_dquot *dq = ip->i_dquot[type];
	long ncurinodes = dq->dq_curinodes + change;

	/*
	 * If user would exceed their hard limit, disallow inode allocation.
	 */
	if (ncurinodes >= dq->dq_ihardlimit && dq->dq_ihardlimit) {
		if ((dq->dq_flags & DQ_INODS) == 0 &&
		    ip->i_uid == cred->cr_uid) {
			uprintf("\n%s: write failed, %s inode limit reached\n",
			    ITOV(ip)->v_mount->mnt_stat.f_mntfromname,
			    quotatypes[type]);
			dq->dq_flags |= DQ_INODS;
		}
		return (EDQUOT);
	}
	/*
	 * If user is over their soft limit for too long, disallow inode
	 * allocation. Reset time limit as they cross their soft limit.
	 */
	if (ncurinodes >= dq->dq_isoftlimit && dq->dq_isoftlimit) {
		if (dq->dq_curinodes < dq->dq_isoftlimit) {
			dq->dq_itime = time_second +
			    VFSTOEXT2(ITOV(ip)->v_mount)->um_itime[type];
			if (ip->i_uid == cred->cr_uid)
				uprintf("\n%s: warning, %s %s\n",
				    ITOV(ip)->v_mount->mnt_stat.f_mntfromname,
				    quotatypes[type], "inode quota exceeded");
			return (0);
		}
		if (time_second > dq->dq_itime) {
			if ((dq->dq_flags & DQ_INODS) == 0 &&
			    ip->i_uid == cred->cr_uid) {
				uprintf("\n%s: write failed, %s %s\n",
				    ITOV(ip)->v_mount->mnt_stat.f_mntfromname,
				    quotatypes[type],
				    "inode quota exceeded for too long");
				dq->dq_flags |= DQ_INODS;
			}
			return (EDQUOT);
		}
	}
	return (0);
}

#ifdef DIAGNOSTIC
/*
 * On filesystems with quotas enabled, it is an error for a file to change
 * size and not to have a dquot structure associated with it.
 */
static void
ext2_chkdquot(struct inode *ip)
{
	struct ext2_mount *ump = VFSTOEXT2(ITOV(ip)->v_mount);
	int i;

	for (i = 0; i < MAXQUOTAS; i++) {
		if (ump->um_quotas[i] == NULLVP ||
		    (ump->um_qflags[i] & (QTF_OPENING|QTF_CLOSING)))
			continue;
		if (ip->i_dquot[i] == NODQUOT) {
			vprint("chkdquot: missing dquot", ITOV(ip));
			panic("chkdquot: missing dquot");
		}
	}
}
#endif

/*
 * Code to process quotactl commands.
 */

struct scaninfo {
	int rescan;
	int type;
};

/*
 * Q_QUOTAON - set up a quota file for a particular filesystem.
 */
static int ext2_quotaon_scan(struct mount *mp, struct vnode *vp, void *data);

int
ext2_quotaon(struct ucred *cred, struct mount *mp, int type, caddr_t fname)
{
	struct ext2_mount *ump = VFSTOEXT2(mp);
	struct vnode *vp, **vpp;
	struct ext2_dquot *dq;
	int error;
	struct nlookupdata nd;
	struct scaninfo scaninfo;

	vpp = &ump->um_quotas[type];
	error = nlookup_init(&nd, fname, UIO_USERSPACE, NLC_FOLLOW|NLC_LOCKVP);
	if (error == 0)
		error = vn_open(&nd, NULL, FREAD|FWRITE, 0);
	if (error == 0 && nd.nl_open_vp->v_type != VREG)
		error = EACCES;
	if (error) {
		nlookup_done(&nd);
		return (error);
	}
	vp = nd.nl_open_vp;
	nd.nl_open_vp = NULL;
	nlookup_done(&nd);

	vn_unlock(vp);
	if (*vpp != vp)
		ext2_quotaoff(mp, type);
	ump->um_qflags[type] |= QTF_OPENING;
	mp->mnt_flag |= MNT_QUOTA;
	vsetflags(vp, VSYSTEM);
	*vpp = vp;
	/* XXX release duplicate vp if *vpp == vp? */
	/*
	 * Save the credential of the process that turned on quotas.
	 * Set up the time limits for this quota.
	 */
	ump->um_cred[type] = crhold(cred);
	ump->um_btime[type] = MAX_DQ_TIME;
	ump->um_itime[type] = MAX_IQ_TIME;
	if (ext2_dqget(NULLVP, 0, ump, type, &dq) == 0) {
		if (dq->dq_btime > 0)
			ump->um_btime[type] = dq->dq_btime;
		if (dq->dq_itime > 0)
			ump->um_itime[type] = dq->dq_itime;
		ext2_dqrele(NULLVP, dq);
	}
	/*
	 * Search vnodes associated with this mount point,
	 * adding references to quota file being opened.
	 * NB: only need to add dquot's for inodes being modified.
	 */
	scaninfo.rescan = 1;
	while (scaninfo.rescan) {
		scaninfo.rescan = 0;
		error = vmntvnodescan(mp, VMSC_GETVP,
					NULL, ext2_quotaon_scan, &scaninfo);
		if (error)
			break;
	}
	ump->um_qflags[type] &= ~QTF_OPENING;
	if (error)
		ext2_quotaoff(mp, type);
	return (error);
}

static int
ext2_quotaon_scan(struct mount *mp, struct vnode *vp, void *data)
{
	int error;
	/*struct scaninfo *info = data;*/

	if (vp->v_writecount == 0)
		return(0);
	error = ext2_getinoquota(VTOI(vp));
	return(error);
}

/*
 * Q_QUOTAOFF - turn off disk quotas for a filesystem.
 */

static int ext2_quotaoff_scan(struct mount *mp, struct vnode *vp, void *data);

int
ext2_quotaoff(struct mount *mp, int type)
{
	struct vnode *qvp;
	struct ext2_mount *ump = VFSTOEXT2(mp);
	int error;
	struct scaninfo scaninfo;

	if ((qvp = ump->um_quotas[type]) == NULLVP)
		return (0);
	ump->um_qflags[type] |= QTF_CLOSING;

	/*
	 * Search vnodes associated with this mount point,
	 * deleting any references to quota file being closed.
	 */
	scaninfo.rescan = 1;
	scaninfo.type = type;
	while (scaninfo.rescan) {
		scaninfo.rescan = 0;
		vmntvnodescan(mp, VMSC_GETVP, NULL, ext2_quotaoff_scan, &scaninfo);
	}
	ext2_dqflush(qvp);
	vclrflags(qvp, VSYSTEM);
	error = vn_close(qvp, FREAD|FWRITE, NULL);
	ump->um_quotas[type] = NULLVP;
	crfree(ump->um_cred[type]);
	ump->um_cred[type] = NOCRED;
	ump->um_qflags[type] &= ~QTF_CLOSING;
	for (type = 0; type < MAXQUOTAS; type++) {
		if (ump->um_quotas[type] != NULLVP)
			break;
	}
	if (type == MAXQUOTAS)
		mp->mnt_flag &= ~MNT_QUOTA;
	return (error);
}

static int
ext2_quotaoff_scan(struct mount *mp, struct vnode *vp, void *data)
{
	struct scaninfo *info = data;
	struct ext2_dquot *dq;
	struct inode *ip;

	if (vp->v_type == VNON) {
		return(0);
	}
	ip = VTOI(vp);
	dq = ip->i_dquot[info->type];
	ip->i_dquot[info->type] = NODQUOT;
	ext2_dqrele(vp, dq);
	return(0);
}

/*
 * Q_GETQUOTA - return current values in a dqblk structure.
 */
int
ext2_getquota(struct mount *mp, u_long id, int type, caddr_t addr)
{
	struct ext2_dquot *dq;
	int error;

	error = ext2_dqget(NULLVP, id, VFSTOEXT2(mp), type, &dq);
	if (error)
		return (error);
	error = copyout((caddr_t)&dq->dq_dqb, addr, sizeof (struct ext2_dqblk));
	ext2_dqrele(NULLVP, dq);
	return (error);
}

/*
 * Q_SETQUOTA - assign an entire dqblk structure.
 */
int
ext2_setquota(struct mount *mp, u_long id, int type, caddr_t addr)
{
	struct ext2_dquot *dq;
	struct ext2_dquot *ndq;
	struct ext2_mount *ump = VFSTOEXT2(mp);
	struct ext2_dqblk newlim;
	int error;

	error = copyin(addr, (caddr_t)&newlim, sizeof (struct ext2_dqblk));
	if (error)
		return (error);
	error = ext2_dqget(NULLVP, id, ump, type, &ndq);
	if (error)
		return (error);
	dq = ndq;
	while (dq->dq_flags & DQ_LOCK) {
		dq->dq_flags |= DQ_WANT;
		(void) tsleep((caddr_t)dq, 0, "setqta", 0);
	}
	/*
	 * Copy all but the current values.
	 * Reset time limit if previously had no soft limit or were
	 * under it, but now have a soft limit and are over it.
	 */
	newlim.dqb_curblocks = dq->dq_curblocks;
	newlim.dqb_curinodes = dq->dq_curinodes;
	if (dq->dq_id != 0) {
		newlim.dqb_btime = dq->dq_btime;
		newlim.dqb_itime = dq->dq_itime;
	}
	if (newlim.dqb_bsoftlimit &&
	    dq->dq_curblocks >= newlim.dqb_bsoftlimit &&
	    (dq->dq_bsoftlimit == 0 || dq->dq_curblocks < dq->dq_bsoftlimit))
		newlim.dqb_btime = time_second + ump->um_btime[type];
	if (newlim.dqb_isoftlimit &&
	    dq->dq_curinodes >= newlim.dqb_isoftlimit &&
	    (dq->dq_isoftlimit == 0 || dq->dq_curinodes < dq->dq_isoftlimit))
		newlim.dqb_itime = time_second + ump->um_itime[type];
	dq->dq_dqb = newlim;
	if (dq->dq_curblocks < dq->dq_bsoftlimit)
		dq->dq_flags &= ~DQ_BLKS;
	if (dq->dq_curinodes < dq->dq_isoftlimit)
		dq->dq_flags &= ~DQ_INODS;
	if (dq->dq_isoftlimit == 0 && dq->dq_bsoftlimit == 0 &&
	    dq->dq_ihardlimit == 0 && dq->dq_bhardlimit == 0)
		dq->dq_flags |= DQ_FAKE;
	else
		dq->dq_flags &= ~DQ_FAKE;
	dq->dq_flags |= DQ_MOD;
	ext2_dqrele(NULLVP, dq);
	return (0);
}

/*
 * Q_SETUSE - set current inode and block usage.
 */
int
ext2_setuse(struct mount *mp, u_long id, int type, caddr_t addr)
{
	struct ext2_dquot *dq;
	struct ext2_mount *ump = VFSTOEXT2(mp);
	struct ext2_dquot *ndq;
	struct ext2_dqblk usage;
	int error;

	error = copyin(addr, (caddr_t)&usage, sizeof (struct ext2_dqblk));
	if (error)
		return (error);
	error = ext2_dqget(NULLVP, id, ump, type, &ndq);
	if (error)
		return (error);
	dq = ndq;
	while (dq->dq_flags & DQ_LOCK) {
		dq->dq_flags |= DQ_WANT;
		(void) tsleep((caddr_t)dq, 0, "setuse", 0);
	}
	/*
	 * Reset time limit if have a soft limit and were
	 * previously under it, but are now over it.
	 */
	if (dq->dq_bsoftlimit && dq->dq_curblocks < dq->dq_bsoftlimit &&
	    usage.dqb_curblocks >= dq->dq_bsoftlimit)
		dq->dq_btime = time_second + ump->um_btime[type];
	if (dq->dq_isoftlimit && dq->dq_curinodes < dq->dq_isoftlimit &&
	    usage.dqb_curinodes >= dq->dq_isoftlimit)
		dq->dq_itime = time_second + ump->um_itime[type];
	dq->dq_curblocks = usage.dqb_curblocks;
	dq->dq_curinodes = usage.dqb_curinodes;
	if (dq->dq_curblocks < dq->dq_bsoftlimit)
		dq->dq_flags &= ~DQ_BLKS;
	if (dq->dq_curinodes < dq->dq_isoftlimit)
		dq->dq_flags &= ~DQ_INODS;
	dq->dq_flags |= DQ_MOD;
	ext2_dqrele(NULLVP, dq);
	return (0);
}

/*
 * Q_SYNC - sync quota files to disk.
 */

static int ext2_qsync_scan(struct mount *mp, struct vnode *vp, void *data);

int
ext2_qsync(struct mount *mp)
{
	struct ext2_mount *ump = VFSTOEXT2(mp);
	struct scaninfo scaninfo;
	int i;

	/*
	 * Check if the mount point has any quotas.
	 * If not, simply return.
	 */
	for (i = 0; i < MAXQUOTAS; i++)
		if (ump->um_quotas[i] != NULLVP)
			break;
	if (i == MAXQUOTAS)
		return (0);
	/*
	 * Search vnodes associated with this mount point,
	 * synchronizing any modified ext2_dquot structures.
	 */
	scaninfo.rescan = 1;
	while (scaninfo.rescan) {
		scaninfo.rescan = 0;
		vmntvnodescan(mp, VMSC_GETVP|VMSC_NOWAIT,
				NULL, ext2_qsync_scan, &scaninfo);
	}
	return (0);
}

static int
ext2_qsync_scan(struct mount *mp, struct vnode *vp, void *data)
{
	/*struct scaninfo *info = data;*/
	struct ext2_dquot *dq;
	/* int error;*/
	int i;

	for (i = 0; i < MAXQUOTAS; i++) {
		dq = VTOI(vp)->i_dquot[i];
		if (dq != NODQUOT && (dq->dq_flags & DQ_MOD))
			ext2_dqsync(vp, dq);
	}
	return(0);
}

/*
 * Code pertaining to management of the in-core dquot data structures.
 */
#define DQHASH(dqvp, id) \
	(&ext2_dqhashtbl[((((intptr_t)(dqvp)) >> 8) + id) & ext2_dqhash])
static LIST_HEAD(ext2_dqhash, ext2_dquot) *ext2_dqhashtbl;
static u_long ext2_dqhash;

/*
 * Dquot free list.
 */
#define	DQUOTINC	5	/* minimum free dquots desired */
static TAILQ_HEAD(ext2_dqfreelist, ext2_dquot) ext2_dqfreelist;
static long ext2_numdquot, ext2_desireddquot = DQUOTINC;

/*
 * Initialize the quota system.
 */
void
ext2_dqinit(void)
{
	ext2_dqhashtbl = hashinit(desiredvnodes, M_EXT2DQUOT, &ext2_dqhash);
	TAILQ_INIT(&ext2_dqfreelist);
}

/*
 * Obtain a dquot structure for the specified identifier and quota file
 * reading the information from the file if necessary.
 */
static int
ext2_dqget(struct vnode *vp, u_long id, struct ext2_mount *ump, int type,
      struct ext2_dquot **dqp)
{
	struct ext2_dquot *dq;
	struct ext2_dqhash *dqh;
	struct vnode *dqvp;
	struct iovec aiov;
	struct uio auio;
	int error;

	dqvp = ump->um_quotas[type];
	if (dqvp == NULLVP || (ump->um_qflags[type] & QTF_CLOSING)) {
		*dqp = NODQUOT;
		return (EINVAL);
	}
	/*
	 * Check the cache first.
	 */
	dqh = DQHASH(dqvp, id);
	LIST_FOREACH(dq, dqh, dq_hash) {
		if (dq->dq_id != id ||
		    dq->dq_ump->um_quotas[dq->dq_type] != dqvp)
			continue;
		/*
		 * Cache hit with no references.  Take
		 * the structure off the free list.
		 */
		if (dq->dq_cnt == 0)
			TAILQ_REMOVE(&ext2_dqfreelist, dq, dq_freelist);
		DQREF(dq);
		*dqp = dq;
		return (0);
	}
	/*
	 * Not in cache, allocate a new one.
	 */
	if (TAILQ_EMPTY(&ext2_dqfreelist) && ext2_numdquot < MAXQUOTAS * desiredvnodes)
		ext2_desireddquot += DQUOTINC;
	if (ext2_numdquot < ext2_desireddquot) {
		dq = (struct ext2_dquot *)kmalloc(sizeof *dq, M_EXT2DQUOT, M_WAITOK | M_ZERO);
		ext2_numdquot++;
	} else {
		if ((dq = TAILQ_FIRST(&ext2_dqfreelist)) == NULL) {
			tablefull("dquot");
			*dqp = NODQUOT;
			return (EUSERS);
		}
		if (dq->dq_cnt || (dq->dq_flags & DQ_MOD))
			panic("dqget: free dquot isn't");
		TAILQ_REMOVE(&ext2_dqfreelist, dq, dq_freelist);
		if (dq->dq_ump != NULL)
			LIST_REMOVE(dq, dq_hash);
	}
	/*
	 * Initialize the contents of the dquot structure.
	 */
	if (vp != dqvp)
		vn_lock(dqvp, LK_EXCLUSIVE | LK_RETRY);
	LIST_INSERT_HEAD(dqh, dq, dq_hash);
	DQREF(dq);
	dq->dq_flags = DQ_LOCK;
	dq->dq_id = id;
	dq->dq_ump = ump;
	dq->dq_type = type;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	aiov.iov_base = (caddr_t)&dq->dq_dqb;
	aiov.iov_len = sizeof (struct ext2_dqblk);
	auio.uio_resid = sizeof (struct ext2_dqblk);
	auio.uio_offset = (off_t)(id * sizeof (struct ext2_dqblk));
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_rw = UIO_READ;
	auio.uio_td = NULL;
	error = VOP_READ(dqvp, &auio, 0, ump->um_cred[type]);
	if (auio.uio_resid == sizeof(struct ext2_dqblk) && error == 0)
		bzero((caddr_t)&dq->dq_dqb, sizeof(struct ext2_dqblk));
	if (vp != dqvp)
		vn_unlock(dqvp);
	if (dq->dq_flags & DQ_WANT)
		wakeup((caddr_t)dq);
	dq->dq_flags = 0;
	/*
	 * I/O error in reading quota file, release
	 * quota structure and reflect problem to caller.
	 */
	if (error) {
		LIST_REMOVE(dq, dq_hash);
		ext2_dqrele(vp, dq);
		*dqp = NODQUOT;
		return (error);
	}
	/*
	 * Check for no limit to enforce.
	 * Initialize time values if necessary.
	 */
	if (dq->dq_isoftlimit == 0 && dq->dq_bsoftlimit == 0 &&
	    dq->dq_ihardlimit == 0 && dq->dq_bhardlimit == 0)
		dq->dq_flags |= DQ_FAKE;
	if (dq->dq_id != 0) {
		if (dq->dq_btime == 0)
			dq->dq_btime = time_second + ump->um_btime[type];
		if (dq->dq_itime == 0)
			dq->dq_itime = time_second + ump->um_itime[type];
	}
	*dqp = dq;
	return (0);
}

#ifdef DIAGNOSTIC
/*
 * Obtain a reference to a dquot.
 */
static void
ext2_dqref(struct ext2_dquot *dq)
{
	dq->dq_cnt++;
}
#endif

/*
 * Release a reference to a dquot.
 */
void
ext2_dqrele(struct vnode *vp, struct ext2_dquot *dq)
{
	if (dq == NODQUOT)
		return;
	if (dq->dq_cnt > 1) {
		dq->dq_cnt--;
		return;
	}
	if (dq->dq_flags & DQ_MOD)
		(void)ext2_dqsync(vp, dq);
	if (--dq->dq_cnt > 0)
		return;
	TAILQ_INSERT_TAIL(&ext2_dqfreelist, dq, dq_freelist);
}

/*
 * Update the disk quota in the quota file.
 */
static int
ext2_dqsync(struct vnode *vp, struct ext2_dquot *dq)
{
	struct vnode *dqvp;
	struct iovec aiov;
	struct uio auio;
	int error;

	if (dq == NODQUOT)
		panic("dqsync: dquot");
	if ((dq->dq_flags & DQ_MOD) == 0)
		return (0);
	if ((dqvp = dq->dq_ump->um_quotas[dq->dq_type]) == NULLVP)
		panic("dqsync: file");
	if (vp != dqvp)
		vn_lock(dqvp, LK_EXCLUSIVE | LK_RETRY);
	while (dq->dq_flags & DQ_LOCK) {
		dq->dq_flags |= DQ_WANT;
		(void) tsleep((caddr_t)dq, 0, "dqsync", 0);
		if ((dq->dq_flags & DQ_MOD) == 0) {
			if (vp != dqvp)
				vn_unlock(dqvp);
			return (0);
		}
	}
	dq->dq_flags |= DQ_LOCK;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	aiov.iov_base = (caddr_t)&dq->dq_dqb;
	aiov.iov_len = sizeof (struct ext2_dqblk);
	auio.uio_resid = sizeof (struct ext2_dqblk);
	auio.uio_offset = (off_t)(dq->dq_id * sizeof (struct ext2_dqblk));
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_rw = UIO_WRITE;
	auio.uio_td = NULL;
	error = VOP_WRITE(dqvp, &auio, 0, dq->dq_ump->um_cred[dq->dq_type]);
	if (auio.uio_resid && error == 0)
		error = EIO;
	if (dq->dq_flags & DQ_WANT)
		wakeup((caddr_t)dq);
	dq->dq_flags &= ~(DQ_MOD|DQ_LOCK|DQ_WANT);
	if (vp != dqvp)
		vn_unlock(dqvp);
	return (error);
}

/*
 * Flush all entries from the cache for a particular vnode.
 */
static void
ext2_dqflush(struct vnode *vp)
{
	struct ext2_dquot *dq, *nextdq;
	struct ext2_dqhash *dqh;

	/*
	 * Move all dquot's that used to refer to this quota
	 * file off their hash chains (they will eventually
	 * fall off the head of the free list and be re-used).
	 */
	for (dqh = &ext2_dqhashtbl[ext2_dqhash]; dqh >= ext2_dqhashtbl; dqh--) {
		for (dq = dqh->lh_first; dq; dq = nextdq) {
			nextdq = dq->dq_hash.le_next;
			if (dq->dq_ump->um_quotas[dq->dq_type] != vp)
				continue;
			if (dq->dq_cnt)
				panic("dqflush: stray dquot");
			LIST_REMOVE(dq, dq_hash);
			dq->dq_ump = NULL;
		}
	}
}
