/*
 * Copyright (c) 2000-2001, Boris Popov
 * All rights reserved.
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
 *    This product includes software developed by Boris Popov.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/fs/smbfs/smbfs_vfsops.c,v 1.2.2.5 2003/01/17 08:20:26 tjr Exp $
 */

#ifndef KLD_MODULE
#include "opt_netsmb.h"
#ifndef NETSMB
#error "SMBFS requires option NETSMB"
#endif
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/malloc.h>
#include <sys/module.h>


#include <netproto/smb/smb.h>
#include <netproto/smb/smb_conn.h>
#include <netproto/smb/smb_subr.h>
#include <netproto/smb/smb_dev.h>

#include "smbfs.h"
#include "smbfs_node.h"
#include "smbfs_subr.h"

#include <sys/buf.h>

extern struct vop_ops smbfs_vnode_vops;

int smbfs_debuglevel = 0;

static int smbfs_version = SMBFS_VERSION;

SYSCTL_NODE(_vfs, OID_AUTO, smbfs, CTLFLAG_RW, 0, "SMB/CIFS file system");
SYSCTL_INT(_vfs_smbfs, OID_AUTO, version, CTLFLAG_RD, &smbfs_version, 0, "");
SYSCTL_INT(_vfs_smbfs, OID_AUTO, debuglevel, CTLFLAG_RW, &smbfs_debuglevel, 0, "");

static MALLOC_DEFINE(M_SMBFSHASH, "SMBFS hash", "SMBFS hash table");


static int smbfs_mount(struct mount *, char *, caddr_t, struct ucred *);
static int smbfs_root(struct mount *, struct vnode **);
static int smbfs_statfs(struct mount *, struct statfs *, struct ucred *);
static int smbfs_sync(struct mount *, int);
static int smbfs_unmount(struct mount *, int);
static int smbfs_init(struct vfsconf *vfsp);
static int smbfs_uninit(struct vfsconf *vfsp);

static struct vfsops smbfs_vfsops = {
	.vfs_mount =    	smbfs_mount,
	.vfs_unmount =    	smbfs_unmount,
	.vfs_root =    		smbfs_root,
	.vfs_statfs =    	smbfs_statfs,
	.vfs_sync =    		smbfs_sync,
	.vfs_init =    		smbfs_init,
	.vfs_uninit =    	smbfs_uninit
};


VFS_SET(smbfs_vfsops, smbfs, VFCF_NETWORK);
MODULE_VERSION(smbfs, 1);

MODULE_DEPEND(smbfs, netsmb, NSMB_VERSION, NSMB_VERSION, NSMB_VERSION);
MODULE_DEPEND(smbfs, libiconv, 1, 1, 2);
MODULE_DEPEND(smbfs, libmchain, 1, 1, 1);

long smbfs_pbuf_freecnt = -1;	/* start out unlimited */

static int
smbfs_mount(struct mount *mp, char *path, caddr_t data, struct ucred *cred)
{
	struct smbfs_args args; 	  /* will hold data from mount request */
	struct smbmount *smp = NULL;
	struct smb_vc *vcp;
	struct smb_share *ssp = NULL;
	struct vnode *vp;
	struct smb_cred scred;
	int error;
	char *pc, *pe;

	if (data == NULL) {
		kprintf("missing data argument\n");
		return EINVAL;
	}
	if (mp->mnt_flag & MNT_UPDATE) {
		kprintf("MNT_UPDATE not implemented");
		return EOPNOTSUPP;
	}
	error = copyin(data, (caddr_t)&args, sizeof(struct smbfs_args));
	if (error)
		return error;
	if (args.version != SMBFS_VERSION) {
		kprintf("mount version mismatch: kernel=%d, mount=%d\n",
		    SMBFS_VERSION, args.version);
		return EINVAL;
	}
	smb_makescred(&scred, curthread, cred);
	error = smb_dev2share(args.dev, SMBM_EXEC, &scred, &ssp);
	if (error) {
		kprintf("invalid device handle %d (%d)\n", args.dev, error);
		return error;
	}
	vcp = SSTOVC(ssp);
	smb_share_unlock(ssp, 0);
	mp->mnt_stat.f_iosize = SSTOVC(ssp)->vc_txmax;

        smp = kmalloc(sizeof(*smp), M_SMBFSDATA, M_WAITOK | M_USE_RESERVE);
        if (smp == NULL) {
                kprintf("could not alloc smbmount\n");
                error = ENOMEM;
		goto bad;
        }
	bzero(smp, sizeof(*smp));
        mp->mnt_data = (qaddr_t)smp;
	smp->sm_cred = crhold(cred);
	smp->sm_hash = hashinit(desiredvnodes, M_SMBFSHASH, &smp->sm_hashlen);
	if (smp->sm_hash == NULL)
		goto bad;
	lockinit(&smp->sm_hashlock, "smbfsh", 0, 0);
	smp->sm_share = ssp;
	smp->sm_root = NULL;
        smp->sm_args = args;
	smp->sm_caseopt = args.caseopt;
	smp->sm_args.file_mode = (smp->sm_args.file_mode &
			    (S_IRWXU|S_IRWXG|S_IRWXO)) | S_IFREG;
	smp->sm_args.dir_mode  = (smp->sm_args.dir_mode &
			    (S_IRWXU|S_IRWXG|S_IRWXO)) | S_IFDIR;

/*	simple_lock_init(&smp->sm_npslock);*/
	pc = mp->mnt_stat.f_mntfromname;
	pe = pc + sizeof(mp->mnt_stat.f_mntfromname);
	bzero(pc, MNAMELEN);
	*pc++ = '/';
	*pc++ = '/';
	pc=index(strncpy(pc, vcp->vc_username, pe - pc - 2), 0);
	if (pc < pe-1) {
		*(pc++) = '@';
		pc = index(strncpy(pc, vcp->vc_srvname, pe - pc - 2), 0);
		if (pc < pe - 1) {
			*(pc++) = '/';
			strncpy(pc, ssp->ss_name, pe - pc - 2);
		}
	}
	/* protect against invalid mount points */
	smp->sm_args.mount_point[sizeof(smp->sm_args.mount_point) - 1] = '\0';
	vfs_getnewfsid(mp);

	vfs_add_vnodeops(mp, &smbfs_vnode_vops, &mp->mnt_vn_norm_ops);

	error = smbfs_root(mp, &vp);
	if (error)
		goto bad;
	vn_unlock(vp);
	SMBVDEBUG("root.v_refcnt = %08x\n", vp->v_refcnt);

#ifdef DIAGNOSTICS
	SMBERROR("mp=%p\n", mp);
#endif
	return error;
bad:
        if (smp) {
		if (smp->sm_cred)
			crfree(smp->sm_cred);
		if (smp->sm_hash)
			hashdestroy(smp->sm_hash, M_SMBFSHASH,
			    smp->sm_hashlen);
		lockdestroy(&smp->sm_hashlock);
		kfree(smp, M_SMBFSDATA);
	}
	if (ssp)
		smb_share_put(ssp, &scred);
        return error;
}

/* Unmount the filesystem described by mp. */
static int
smbfs_unmount(struct mount *mp, int mntflags)
{
	struct smbmount *smp = VFSTOSMBFS(mp);
	struct smb_cred scred;
	int error, flags;

	SMBVDEBUG("smbfs_unmount: flags=%04x\n", mntflags);
	flags = 0;
	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;
	/*
	 * Keep trying to flush the vnode list for the mount while 
	 * some are still busy and we are making progress towards
	 * making them not busy. This is needed because smbfs vnodes
	 * reference their parent directory but may appear after their
	 * parent in the list; one pass over the vnode list is not
	 * sufficient in this case.
	 */
	do {
		smp->sm_didrele = 0;
		/* There is 1 extra root vnode reference from smbfs_mount(). */
		error = vflush(mp, 1, flags);
	} while (error == EBUSY && smp->sm_didrele != 0);
	if (error)
		return error;
	smb_makescred(&scred, curthread, smp->sm_cred);
	smb_share_put(smp->sm_share, &scred);
	mp->mnt_data = (qaddr_t)0;

	if (smp->sm_cred)
		crfree(smp->sm_cred);
	if (smp->sm_hash)
		hashdestroy(smp->sm_hash, M_SMBFSHASH, smp->sm_hashlen);
	lockdestroy(&smp->sm_hashlock);
	kfree(smp, M_SMBFSDATA);
	mp->mnt_flag &= ~MNT_LOCAL;
	return error;
}

/* 
 * Return locked root vnode of a filesystem
 */
static int
smbfs_root(struct mount *mp, struct vnode **vpp)
{
	struct thread *td = curthread;	/* XXX */
	struct smbmount *smp = VFSTOSMBFS(mp);
	struct vnode *vp;
	struct smbnode *np;
	struct smbfattr fattr;
	struct ucred *cred;
	struct smb_cred scred;
	int error;

	if (smp == NULL) {
		SMBERROR("smp == NULL (bug in umount)\n");
		return EINVAL;
	}
	if (smp->sm_root) {
		*vpp = SMBTOV(smp->sm_root);
		return vget(*vpp, LK_EXCLUSIVE | LK_RETRY);
	}
	if (td->td_proc)
		cred = td->td_proc->p_ucred;
	else
		cred = proc0.p_ucred;

	smb_makescred(&scred, td, cred);
	error = smbfs_smb_lookup(NULL, NULL, 0, &fattr, &scred);
	if (error)
		return error;
	error = smbfs_nget(mp, NULL, "TheRooT", 7, &fattr, &vp);
	if (error)
		return error;
	vsetflags(vp, VROOT);
	np = VTOSMB(vp);
	smp->sm_root = np;
	*vpp = vp;
	return 0;
}

/*ARGSUSED*/
int
smbfs_init(struct vfsconf *vfsp)
{
	smbfs_pbuf_freecnt = nswbuf / 2 + 1;
	SMBVDEBUG("done.\n");
	return 0;
}

/*ARGSUSED*/
int
smbfs_uninit(struct vfsconf *vfsp)
{
	SMBVDEBUG("done.\n");
	return 0;
}

/*
 * smbfs_statfs call
 */
int
smbfs_statfs(struct mount *mp, struct statfs *sbp, struct ucred *cred)
{
	struct smbmount *smp = VFSTOSMBFS(mp);
	struct smbnode *np = smp->sm_root;
	struct smb_share *ssp = smp->sm_share;
	struct smb_cred scred;
	int error = 0;

	if (np == NULL)
		return EINVAL;
	
	sbp->f_iosize = SSTOVC(ssp)->vc_txmax;		/* optimal transfer block size */
	sbp->f_spare2 = 0;			/* placeholder */
	smb_makescred(&scred, curthread, cred);

	if (SMB_DIALECT(SSTOVC(ssp)) >= SMB_DIALECT_LANMAN2_0)
		error = smbfs_smb_statfs2(ssp, sbp, &scred);
	else
		error = smbfs_smb_statfs(ssp, sbp, &scred);
	if (error)
		return error;
	sbp->f_flags = 0;		/* copy of mount exported flags */
	if (sbp != &mp->mnt_stat) {
		sbp->f_fsid = mp->mnt_stat.f_fsid;	/* file system id */
		sbp->f_owner = mp->mnt_stat.f_owner;	/* user that mounted the filesystem */
		sbp->f_type = mp->mnt_vfc->vfc_typenum;	/* type of filesystem */
		bcopy(mp->mnt_stat.f_mntfromname, sbp->f_mntfromname, MNAMELEN);
	}
	strncpy(sbp->f_fstypename, mp->mnt_vfc->vfc_name, MFSNAMELEN);
	return 0;
}

/*
 * Flush out the buffer cache
 */
/* ARGSUSED */
static int
smbfs_sync(struct mount *mp, int waitfor)
{
	struct vnode *vp;
	int error, allerror = 0;
	/*
	 * Force stale buffer cache information to be flushed.
	 */
loop:
	for (vp = TAILQ_FIRST(&mp->mnt_nvnodelist);
	     vp != NULL;
	     vp = TAILQ_NEXT(vp, v_nmntvnodes)) {
		/*
		 * If the vnode that we are about to sync is no longer
		 * associated with this mount point, start over.
		 */
		if (vp->v_mount != mp)
			goto loop;
		if (vn_islocked(vp) || RB_EMPTY(&vp->v_rbdirty_tree) ||
		    (waitfor & MNT_LAZY))
			continue;
		if (vget(vp, LK_EXCLUSIVE))
			goto loop;
		error = VOP_FSYNC(vp, waitfor, 0);
		if (error)
			allerror = error;
		vput(vp);
	}
	return (allerror);
}
