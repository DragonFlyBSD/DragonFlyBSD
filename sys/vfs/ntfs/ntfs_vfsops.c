/*	$NetBSD: ntfs_vfsops.c,v 1.23 1999/11/15 19:38:14 jdolecek Exp $	*/

/*-
 * Copyright (c) 1998, 1999 Semen Ustimenko
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
 * $FreeBSD: src/sys/ntfs/ntfs_vfsops.c,v 1.20.2.5 2001/12/25 01:44:45 dillon Exp $
 */


#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/proc.h>
#include <sys/nlookup.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/buf.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>
#if defined(__NetBSD__)
#include <sys/device.h>
#endif

#include <machine/inttypes.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#if defined(__NetBSD__)
#include <vm/vm_prot.h>
#endif
#include <vm/vm_page.h>
#include <vm/vm_object.h>
#include <vm/vm_extern.h>
#include <vm/vm_zone.h>

#if defined(__NetBSD__)
#include <miscfs/specfs/specdev.h>
#endif

#include <sys/buf2.h>

#include "ntfs.h"
#include "ntfs_inode.h"
#include "ntfs_subr.h"
#include "ntfs_vfsops.h"
#include "ntfs_ihash.h"
#include "ntfsmount.h"

extern struct vop_ops ntfs_vnode_vops;

#if defined(__DragonFly__)
MALLOC_DEFINE(M_NTFSMNT, "NTFS mount", "NTFS mount structure");
MALLOC_DEFINE(M_NTFSNTNODE,"NTFS ntnode",  "NTFS ntnode information");
MALLOC_DEFINE(M_NTFSFNODE,"NTFS fnode",  "NTFS fnode information");
MALLOC_DEFINE(M_NTFSDIR,"NTFS dir",  "NTFS dir buffer");
#endif

struct iconv_functions *ntfs_iconv = NULL;

static int	ntfs_root (struct mount *, struct vnode **);
static int	ntfs_statfs (struct mount *, struct statfs *,
				struct ucred *cred);
static int	ntfs_statvfs (struct mount *, struct statvfs *,
				struct ucred *);
static int	ntfs_unmount (struct mount *, int);
static int	ntfs_vget (struct mount *mp, struct vnode *dvp,
				ino_t ino, struct vnode **vpp);
static int	ntfs_mountfs (struct vnode *, struct mount *, 
				struct ntfs_args *, struct ucred *);
static int	ntfs_vptofh (struct vnode *, struct fid *);
static int	ntfs_fhtovp (struct mount *, struct vnode *rootvp,
				struct fid *, struct vnode **);

#if !defined (__DragonFly__)
static int	ntfs_quotactl (struct mount *, int, uid_t, caddr_t,
				   struct thread *);
static int	ntfs_start (struct mount *, int, struct thread *);
static int	ntfs_sync (struct mount *, int, struct ucred *,
			       struct thread *);
#endif

#if defined(__DragonFly__)
struct sockaddr;
static int	ntfs_mount (struct mount *, char *, caddr_t, struct ucred *);
static int	ntfs_init (struct vfsconf *);
static int	ntfs_checkexp (struct mount *, struct sockaddr *,
				   int *, struct ucred **);
#elif defined(__NetBSD__)
static int	ntfs_mount (struct mount *, const char *, void *,
				struct nameidata *, struct thread *);
static void	ntfs_init (void);
static int	ntfs_mountroot (void);
static int	ntfs_sysctl (int *, u_int, void *, size_t *, void *,
				 size_t, struct thread *);
static int	ntfs_checkexp (struct mount *, struct mbuf *,
				   int *, struct ucred **);
#endif

/*
 * Verify a remote client has export rights and return these rights via.
 * exflagsp and credanonp.
 */
static int
ntfs_checkexp(struct mount *mp,
#if defined(__DragonFly__)
	      struct sockaddr *nam,
#else /* defined(__NetBSD__) */
	      struct mbuf *nam,
#endif
	      int *exflagsp, struct ucred **credanonp)
{
	struct netcred *np;
	struct ntfsmount *ntm = VFSTONTFS(mp);

	/*
	 * Get the export permission structure for this <mp, client> tuple.
	 */
	np = vfs_export_lookup(mp, &ntm->ntm_export, nam);
	if (np == NULL)
		return (EACCES);

	*exflagsp = np->netc_exflags;
	*credanonp = &np->netc_anon;
	return (0);
}

#if defined(__NetBSD__)
/*ARGSUSED*/
static int
ntfs_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
	    size_t newlen, struct thread *td)
{
	return (EINVAL);
}

static int
ntfs_mountroot(void)
{
	struct mount *mp;
	struct vnode *rootvp;
	struct thread *td = curthread;	/* XXX */
	struct ntfs_args args;
	int error;

	if (root_device->dv_class != DV_DISK)
		return (ENODEV);

	/*
	 * Get vnodes for rootdev.
	 */
	if (bdevvp(rootdev, &rootvp))
		panic("ntfs_mountroot: can't setup rootvp");

	if ((error = vfs_rootmountalloc(MOUNT_NTFS, "root_device", &mp))) {
		vrele(rootvp);
		return (error);
	}

	args.flag = 0;
	args.uid = 0;
	args.gid = 0;
	args.mode = 0777;

	if ((error = ntfs_mountfs(rootvp, mp, &args, proc0.p_ucred)) != 0) {
		mp->mnt_op->vfs_refcount--;
		vfs_unbusy(mp);
		kfree(mp, M_MOUNT);
		vrele(rootvp);
		return (error);
	}
	mountlist_insert(mp, MNTINS_LAST);
	(void)ntfs_statfs(mp, &mp->mnt_stat, proc0.p_ucred);
	vfs_unbusy(mp);
	return (0);
}

static void
ntfs_init(void)
{
	ntfs_nthashinit();
	ntfs_toupper_init();
}

#elif defined(__DragonFly__)

static int
ntfs_init(struct vfsconf *vcp)
{
	ntfs_nthashinit();
	ntfs_toupper_init();
	return 0;
}

#endif /* NetBSD */

static int
ntfs_mount(struct mount *mp, char *path, caddr_t data, struct ucred *cred)
{
	size_t		size;
	int		error;
	struct vnode	*devvp;
	struct ntfs_args args;
	struct nlookupdata nd;
	struct vnode *rootvp;

	error = 0;
#ifdef __DragonFly__
	/*
	 * Use NULL path to flag a root mount
	 */
	if( path == NULL) {
		/*
		 ***
		 * Mounting root file system
		 ***
		 */
	
		/* Get vnode for root device*/
		if( bdevvp( rootdev, &rootvp))
			panic("ffs_mountroot: can't setup bdevvp for root");

		/*
		 * FS specific handling
		 */
		mp->mnt_flag |= MNT_RDONLY;	/* XXX globally applicable?*/

		/*
		 * Attempt mount
		 */
		if( ( error = ntfs_mountfs(rootvp, mp, &args, cred)) != 0) {
			/* fs specific cleanup (if any)*/
			goto error_1;
		}

		goto dostatfs;		/* success*/

	}
#endif /* FreeBSD */

	/*
	 ***
	 * Mounting non-root file system or updating a file system
	 ***
	 */

	/* copy in user arguments*/
	error = copyin(data, (caddr_t)&args, sizeof (struct ntfs_args));
	if (error)
		goto error_1;		/* can't get arguments*/

	/*
	 * If updating, check whether changing from read-only to
	 * read/write; if there is no device name, that's all we do.
	 */
	if (mp->mnt_flag & MNT_UPDATE) {
		/* if not updating name...*/
		if (args.fspec == NULL) {
			/*
			 * Process export requests.  Jumping to "success"
			 * will return the vfs_export() error code.
			 */
			struct ntfsmount *ntm = VFSTONTFS(mp);
			error = vfs_export(mp, &ntm->ntm_export, &args.export);
			goto success;
		}

		kprintf("ntfs_mount(): MNT_UPDATE not supported\n");
		error = EINVAL;
		goto error_1;
	}

	/*
	 * Not an update, or updating the name: look up the name
	 * and verify that it refers to a sensible block device.
	 */
	devvp = NULL;
	error = nlookup_init(&nd, args.fspec, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vref(&nd.nl_nch, nd.nl_cred, &devvp);
	nlookup_done(&nd);
	if (error)
		goto error_1;

#if defined(__DragonFly__)
	if (!vn_isdisk(devvp, &error)) 
		goto error_2;
#else
	if (devvp->v_type != VBLK) {
		error = ENOTBLK;
		goto error_2;
	}
	if (devvp->v_umajor >= nblkdev) {
		error = ENXIO;
		goto error_2;
	}
#endif
	if (mp->mnt_flag & MNT_UPDATE) {
#if 0
		/*
		 ********************
		 * UPDATE
		 ********************
		 */

		if (devvp != ntmp->um_devvp)
			error = EINVAL;	/* needs translation */
		else
			vrele(devvp);
		/*
		 * Update device name only on success
		 */
		if( !error) {
			/* Save "mounted from" info for mount point (NULL pad)*/
			copyinstr(	args.fspec,
					mp->mnt_stat.f_mntfromname,
					MNAMELEN - 1,
					&size);
			bzero( mp->mnt_stat.f_mntfromname + size, MNAMELEN - size);
		}
#endif
	} else {
		/*
		 ********************
		 * NEW MOUNT
		 ********************
		 */

		/* Save "mounted from" info for mount point (NULL pad)*/
		copyinstr(	args.fspec,			/* device name*/
				mp->mnt_stat.f_mntfromname,	/* save area*/
				MNAMELEN - 1,			/* max size*/
				&size);				/* real size*/
		bzero( mp->mnt_stat.f_mntfromname + size, MNAMELEN - size);

		error = ntfs_mountfs(devvp, mp, &args, cred);
	}
	if (error) {
		goto error_2;
	}

#ifdef __DragonFly__
dostatfs:
#endif
	/*
	 * Initialize FS stat information in mount struct; uses
	 * mp->mnt_stat.f_mntfromname.
	 *
	 * This code is common to root and non-root mounts
	 */
	(void)VFS_STATFS(mp, &mp->mnt_stat, cred);

	goto success;


error_2:	/* error with devvp held*/

	/* release devvp before failing*/
	vrele(devvp);

error_1:	/* no state to back out*/

success:
	return(error);
}

/*
 * Common code for mount and mountroot
 */
int
ntfs_mountfs(struct vnode *devvp, struct mount *mp, struct ntfs_args *argsp,
	     struct ucred *cred)
{
	struct buf *bp;
	struct ntfsmount *ntmp;
	cdev_t dev;
	int error, ronly, ncount, i;
	struct vnode *vp;
	char cs_local[ICONV_CSNMAXLEN];
	char cs_ntfs[ICONV_CSNMAXLEN];

	/*
	 * Disallow multiple mounts of the same device.
	 * Disallow mounting of a device that is currently in use
	 * (except for root, which might share swap device for miniroot).
	 * Flush out any old buffers remaining from a previous use.
	 */
	error = vfs_mountedon(devvp);
	if (error)
		return (error);
	ncount = vcount(devvp);
#if defined(__DragonFly__)
	if (devvp->v_object)
		ncount -= 1;
#endif
	if (ncount > 1)
		return (EBUSY);
#if defined(__DragonFly__)
	VN_LOCK(devvp, LK_EXCLUSIVE | LK_RETRY);
	error = vinvalbuf(devvp, V_SAVE, 0, 0);
	VOP__UNLOCK(devvp, 0);
#else
	error = vinvalbuf(devvp, V_SAVE, 0, 0);
#endif
	if (error)
		return (error);

	ronly = (mp->mnt_flag & MNT_RDONLY) != 0;
	VN_LOCK(devvp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_OPEN(devvp, ronly ? FREAD : FREAD|FWRITE, FSCRED, NULL);
	VOP__UNLOCK(devvp, 0);
	if (error)
		return (error);
	dev = devvp->v_rdev;

	bp = NULL;

	error = bread(devvp, BBLOCK, BBSIZE, &bp);
	if (error)
		goto out;
	ntmp = kmalloc(sizeof *ntmp, M_NTFSMNT, M_WAITOK | M_ZERO);
	bcopy( bp->b_data, &ntmp->ntm_bootfile, sizeof(struct bootfile) );
	brelse( bp );
	bp = NULL;

	if (strncmp(ntmp->ntm_bootfile.bf_sysid, NTFS_BBID, NTFS_BBIDLEN)) {
		error = EINVAL;
		dprintf(("ntfs_mountfs: invalid boot block\n"));
		goto out;
	}

	{
		int8_t cpr = ntmp->ntm_mftrecsz;
		if( cpr > 0 )
			ntmp->ntm_bpmftrec = ntmp->ntm_spc * cpr;
		else
			ntmp->ntm_bpmftrec = (1 << (-cpr)) / ntmp->ntm_bps;
	}
	dprintf(("ntfs_mountfs(): bps: %d, spc: %d, media: %x, mftrecsz: %d (%d sects)\n",
		ntmp->ntm_bps,ntmp->ntm_spc,ntmp->ntm_bootfile.bf_media,
		ntmp->ntm_mftrecsz,ntmp->ntm_bpmftrec));
	dprintf(("ntfs_mountfs(): mftcn: 0x%x|0x%x\n",
		(u_int32_t)ntmp->ntm_mftcn,(u_int32_t)ntmp->ntm_mftmirrcn));

	ntmp->ntm_mountp = mp;
	ntmp->ntm_dev = dev;
	ntmp->ntm_devvp = devvp;
	ntmp->ntm_uid = argsp->uid;
	ntmp->ntm_gid = argsp->gid;
	ntmp->ntm_mode = argsp->mode;
	ntmp->ntm_flag = argsp->flag;

	if (argsp->flag & NTFS_MFLAG_KICONV && ntfs_iconv) {
		bcopy(argsp->cs_local, cs_local, sizeof(cs_local));
		bcopy(argsp->cs_ntfs, cs_ntfs, sizeof(cs_ntfs));
		ntfs_82u_init(ntmp, cs_local, cs_ntfs);
		ntfs_u28_init(ntmp, NULL, cs_local, cs_ntfs);
	} else {
		ntfs_82u_init(ntmp, NULL, NULL);
		ntfs_u28_init(ntmp, ntmp->ntm_82u, NULL, NULL);
	}

	mp->mnt_data = (qaddr_t)ntmp;

	dprintf(("ntfs_mountfs(): case-%s,%s uid: %d, gid: %d, mode: %o\n",
		(ntmp->ntm_flag & NTFS_MFLAG_CASEINS)?"insens.":"sens.",
		(ntmp->ntm_flag & NTFS_MFLAG_ALLNAMES)?" allnames,":"",
		ntmp->ntm_uid, ntmp->ntm_gid, ntmp->ntm_mode));

	vfs_add_vnodeops(mp, &ntfs_vnode_vops, &mp->mnt_vn_norm_ops);

	/*
	 * We read in some system nodes to do not allow 
	 * reclaim them and to have everytime access to them.
	 */ 
	{
		int pi[3] = { NTFS_MFTINO, NTFS_ROOTINO, NTFS_BITMAPINO };
		for (i=0; i<3; i++) {
			error = VFS_VGET(mp, NULL,
					 pi[i], &(ntmp->ntm_sysvn[pi[i]]));
			if(error)
				goto out1;
			vsetflags(ntmp->ntm_sysvn[pi[i]], VSYSTEM);
			vref(ntmp->ntm_sysvn[pi[i]]);
			vput(ntmp->ntm_sysvn[pi[i]]);
		}
	}

	/* read the Unicode lowercase --> uppercase translation table,
	 * if necessary */
	if ((error = ntfs_toupper_use(mp, ntmp)))
		goto out1;

	/*
	 * Scan $BitMap and count free clusters
	 */
	error = ntfs_calccfree(ntmp, &ntmp->ntm_cfree);
	if(error)
		goto out1;

	/*
	 * Read and translate to internal format attribute
	 * definition file. 
	 */
	{
		int num,j;
		struct attrdef ad;

		/* Open $AttrDef */
		error = VFS_VGET(mp, NULL, NTFS_ATTRDEFINO, &vp);
		if(error) 
			goto out1;

		/* Count valid entries */
		for(num=0;;num++) {
			error = ntfs_readattr(ntmp, VTONT(vp),
					NTFS_A_DATA, NULL,
					num * sizeof(ad), sizeof(ad),
					&ad, NULL);
			if (error)
				goto out1;
			if (ad.ad_name[0] == 0)
				break;
		}

		/* Alloc memory for attribute definitions */
		ntmp->ntm_ad = kmalloc(num * sizeof(struct ntvattrdef),
				       M_NTFSMNT, M_WAITOK);

		ntmp->ntm_adnum = num;

		/* Read them and translate */
		for(i=0;i<num;i++){
			error = ntfs_readattr(ntmp, VTONT(vp),
					NTFS_A_DATA, NULL,
					i * sizeof(ad), sizeof(ad),
					&ad, NULL);
			if (error)
				goto out1;
			j = 0;
			do {
				ntmp->ntm_ad[i].ad_name[j] = ad.ad_name[j];
			} while(ad.ad_name[j++]);
			ntmp->ntm_ad[i].ad_namelen = j - 1;
			ntmp->ntm_ad[i].ad_type = ad.ad_type;
		}

		vput(vp);
	}

#if defined(__DragonFly__)
	mp->mnt_stat.f_fsid.val[0] = dev2udev(dev);
	mp->mnt_stat.f_fsid.val[1] = mp->mnt_vfc->vfc_typenum;
#else
	mp->mnt_stat.f_fsid.val[0] = dev;
	mp->mnt_stat.f_fsid.val[1] = makefstype(MOUNT_NTFS);
#endif
	mp->mnt_maxsymlinklen = 0;
	mp->mnt_flag |= MNT_LOCAL;
	dev->si_mountpoint = mp;

	return (0);

out1:
	for(i=0;i<NTFS_SYSNODESNUM;i++)
		if(ntmp->ntm_sysvn[i]) vrele(ntmp->ntm_sysvn[i]);

	if (vflush(mp, 0, 0))
		dprintf(("ntfs_mountfs: vflush failed\n"));

out:
	dev->si_mountpoint = NULL;
	if (bp)
		brelse(bp);

#if defined __NetBSD__
	/* lock the device vnode before calling VOP_CLOSE() */
	VN_LOCK(devvp, LK_EXCLUSIVE | LK_RETRY);
	(void)VOP_CLOSE(devvp, ronly ? FREAD : FREAD|FWRITE);
	VOP__UNLOCK(devvp, 0);
#else
	vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY);
	(void)VOP_CLOSE(devvp, ronly ? FREAD : FREAD|FWRITE, NULL);
	vn_unlock(devvp);
#endif
	
	return (error);
}

#if !defined(__DragonFly__)
static int
ntfs_start(struct mount *mp, int flags, struct thread *td)
{
	return (0);
}
#endif

static int
ntfs_unmount(struct mount *mp, int mntflags)
{
	struct ntfsmount *ntmp;
	int error, ronly, flags, i;

	dprintf(("ntfs_unmount: unmounting...\n"));
	ntmp = VFSTONTFS(mp);

	ronly = (mp->mnt_flag & MNT_RDONLY) != 0;
	flags = 0;
	if(mntflags & MNT_FORCE)
		flags |= FORCECLOSE;

	dprintf(("ntfs_unmount: vflushing...\n"));
	error = vflush(mp, 0, flags | SKIPSYSTEM);
	if (error) {
		kprintf("ntfs_unmount: vflush failed: %d\n",error);
		return (error);
	}

	/* Check if only system vnodes are left */
	for(i=0;i<NTFS_SYSNODESNUM;i++)
		 if((ntmp->ntm_sysvn[i]) && 
		    (VREFCNT(ntmp->ntm_sysvn[i]) > 1)) return (EBUSY);

	/* Dereference all system vnodes */
	for(i=0;i<NTFS_SYSNODESNUM;i++)
		 if(ntmp->ntm_sysvn[i]) vrele(ntmp->ntm_sysvn[i]);

	/* vflush system vnodes */
	error = vflush(mp, 0, flags);
	if (error)
		kprintf("ntfs_unmount: vflush failed(sysnodes): %d\n",error);

	/* Check if the type of device node isn't VBAD before
	 * touching v_cdevinfo.  If the device vnode is revoked, the
	 * field is NULL and touching it causes null pointer derefercence.
	 */
	if (ntmp->ntm_devvp->v_type != VBAD)
		ntmp->ntm_devvp->v_rdev->si_mountpoint = NULL;

	vn_lock(ntmp->ntm_devvp, LK_EXCLUSIVE | LK_RETRY);
	vinvalbuf(ntmp->ntm_devvp, V_SAVE, 0, 0);
	error = VOP_CLOSE(ntmp->ntm_devvp, ronly ? FREAD : FREAD|FWRITE, NULL);
	vn_unlock(ntmp->ntm_devvp);

	vrele(ntmp->ntm_devvp);

	/* free the toupper table, if this has been last mounted ntfs volume */
	ntfs_toupper_unuse();

	dprintf(("ntfs_umount: freeing memory...\n"));
	ntfs_u28_uninit(ntmp);
	ntfs_82u_uninit(ntmp);
	mp->mnt_data = (qaddr_t)0;
	mp->mnt_flag &= ~MNT_LOCAL;
	kfree(ntmp->ntm_ad, M_NTFSMNT);
	kfree(ntmp, M_NTFSMNT);
	return (error);
}

static int
ntfs_root(struct mount *mp, struct vnode **vpp)
{
	struct vnode *nvp;
	int error = 0;

	dprintf(("ntfs_root(): sysvn: %p\n",
		VFSTONTFS(mp)->ntm_sysvn[NTFS_ROOTINO]));
	error = VFS_VGET(mp, NULL, (ino_t)NTFS_ROOTINO, &nvp);
	if(error) {
		kprintf("ntfs_root: VFS_VGET failed: %d\n",error);
		return (error);
	}

	*vpp = nvp;
	return (0);
}

#if !defined(__DragonFly__)
static int
ntfs_quotactl(struct mount *mp, int cmds, uid_t uid, caddr_t arg,
	      struct thread *td)
{
	kprintf("\nntfs_quotactl():\n");
	return EOPNOTSUPP;
}
#endif

int
ntfs_calccfree(struct ntfsmount *ntmp, cn_t *cfreep)
{
	struct vnode *vp;
	u_int8_t *tmp;
	int j, error;
	long cfree = 0;
	size_t bmsize, i;

	vp = ntmp->ntm_sysvn[NTFS_BITMAPINO];

	bmsize = VTOF(vp)->f_size;

	tmp = kmalloc(bmsize, M_TEMP, M_WAITOK);

	error = ntfs_readattr(ntmp, VTONT(vp), NTFS_A_DATA, NULL,
			       0, bmsize, tmp, NULL);
	if (error)
		goto out;

	for(i=0;i<bmsize;i++)
		for(j=0;j<8;j++)
			if(~tmp[i] & (1 << j)) cfree++;
	*cfreep = cfree;

    out:
	kfree(tmp, M_TEMP);
	return(error);
}

static int
ntfs_statfs(struct mount *mp, struct statfs *sbp, struct ucred *cred)
{
	struct ntfsmount *ntmp = VFSTONTFS(mp);
	u_int64_t mftallocated;

	dprintf(("ntfs_statfs():\n"));

	mftallocated = VTOF(ntmp->ntm_sysvn[NTFS_MFTINO])->f_allocated;

#if defined(__DragonFly__)
	sbp->f_type = mp->mnt_vfc->vfc_typenum;
#elif defined(__NetBSD__)
	sbp->f_type = 0;
#else
	sbp->f_type = MOUNT_NTFS;
#endif
	sbp->f_bsize = ntmp->ntm_bps;
	sbp->f_iosize = ntmp->ntm_bps * ntmp->ntm_spc;
	sbp->f_blocks = ntmp->ntm_bootfile.bf_spv;
	sbp->f_bfree = sbp->f_bavail = ntfs_cntobn(ntmp->ntm_cfree);
	sbp->f_ffree = sbp->f_bfree / ntmp->ntm_bpmftrec;
	sbp->f_files = mftallocated / ntfs_bntob(ntmp->ntm_bpmftrec) +
		       sbp->f_ffree;
	if (sbp != &mp->mnt_stat) {
		bcopy((caddr_t)mp->mnt_stat.f_mntfromname,
			(caddr_t)&sbp->f_mntfromname[0], MNAMELEN);
	}
	sbp->f_flags = mp->mnt_flag;
#ifdef __NetBSD__
	strncpy(sbp->f_fstypename, mp->mnt_op->vfs_name, MFSNAMELEN);
#endif
	
	return (0);
}

static int
ntfs_statvfs(struct mount *mp, struct statvfs *sbp, struct ucred *cred)
{
	struct ntfsmount *ntmp = VFSTONTFS(mp);
	u_int64_t mftallocated;

	dprintf(("ntfs_statvfs():\n"));

	mftallocated = VTOF(ntmp->ntm_sysvn[NTFS_MFTINO])->f_allocated;

	sbp->f_type = mp->mnt_vfc->vfc_typenum;
	sbp->f_bsize = ntmp->ntm_bps;
	sbp->f_blocks = ntmp->ntm_bootfile.bf_spv;
	sbp->f_bfree = sbp->f_bavail = ntmp->ntm_cfree * ntmp->ntm_spc;
	sbp->f_ffree = sbp->f_bfree / ntmp->ntm_bpmftrec;
	sbp->f_files = mftallocated / (ntmp->ntm_bpmftrec * ntmp->ntm_bps) +
		       sbp->f_ffree;
	
	return (0);
}

#if !defined(__DragonFly__)
static int
ntfs_sync(struct mount *mp, int waitfor, struct ucred *cred, struct thread *td)
{
	/*dprintf(("ntfs_sync():\n"));*/
	return (0);
}
#endif

/*ARGSUSED*/
static int
ntfs_fhtovp(struct mount *mp, struct vnode *rootvp,
	    struct fid *fhp, struct vnode **vpp)
{
	struct vnode *nvp;
	struct ntfid *ntfhp = (struct ntfid *)fhp;
	int error;

	ddprintf(("ntfs_fhtovp(): %ju\n", ntfhp->ntfid_ino));

	if ((error = VFS_VGET(mp, NULL, ntfhp->ntfid_ino, &nvp)) != 0) {
		*vpp = NULLVP;
		return (error);
	}
	/* XXX as unlink/rmdir/mkdir/creat are not currently possible
	 * with NTFS, we don't need to check anything else for now */
	*vpp = nvp;

	return (0);
}

static int
ntfs_vptofh(struct vnode *vp, struct fid *fhp)
{
	struct ntnode *ntp;
	struct ntfid *ntfhp;

	ddprintf(("ntfs_fhtovp(): %p\n", vp));

	ntp = VTONT(vp);
	ntfhp = (struct ntfid *)fhp;
	ntfhp->ntfid_len = sizeof(struct ntfid);
	ntfhp->ntfid_ino = ntp->i_number;
	/* ntfhp->ntfid_gen = ntp->i_gen; */
	return (0);
}

int
ntfs_vgetex(struct mount *mp, ino_t ino, u_int32_t attrtype, char *attrname,
	    u_long lkflags, u_long flags, struct thread *td,
	    struct vnode **vpp) 
{
	int error;
	struct ntfsmount *ntmp;
	struct ntnode *ip;
	struct fnode *fp;
	struct vnode *vp;
	enum vtype f_type;

	dprintf(("ntfs_vgetex: ino: %ju, attr: 0x%x:%s, lkf: 0x%lx, f: 0x%lx\n",
		(uintmax_t) ino, attrtype, attrname?attrname:"", lkflags, flags));

	ntmp = VFSTONTFS(mp);
	*vpp = NULL;

	/* Get ntnode */
	error = ntfs_ntlookup(ntmp, ino, &ip);
	if (error) {
		kprintf("ntfs_vget: ntfs_ntget failed\n");
		return (error);
	}

	/* It may be not initialized fully, so force load it */
	if (!(flags & VG_DONTLOADIN) && !(ip->i_flag & IN_LOADED)) {
		error = ntfs_loadntnode(ntmp, ip);
		if(error) {
			kprintf("ntfs_vget: CAN'T LOAD ATTRIBUTES FOR INO: %"PRId64"\n",
			       ip->i_number);
			ntfs_ntput(ip);
			return (error);
		}
	}

	error = ntfs_fget(ntmp, ip, attrtype, attrname, &fp);
	if (error) {
		kprintf("ntfs_vget: ntfs_fget failed\n");
		ntfs_ntput(ip);
		return (error);
	}

	f_type = VINT;
	if (!(flags & VG_DONTVALIDFN) && !(fp->f_flag & FN_VALID)) {
		if ((ip->i_frflag & NTFS_FRFLAG_DIR) &&
		    (fp->f_attrtype == NTFS_A_DATA && fp->f_attrname == NULL)) {
			f_type = VDIR;
		} else if (flags & VG_EXT) {
			f_type = VINT;
			fp->f_size = fp->f_allocated = 0;
		} else {
			f_type = VREG;	

			error = ntfs_filesize(ntmp, fp, 
					      &fp->f_size, &fp->f_allocated);
			if (error) {
				ntfs_ntput(ip);
				return (error);
			}
		}

		fp->f_flag |= FN_VALID;
	}

	if (FTOV(fp)) {
		VGET(FTOV(fp), lkflags);
		*vpp = FTOV(fp);
		ntfs_ntput(ip);
		return (0);
	}

	error = getnewvnode(VT_NTFS, ntmp->ntm_mountp, &vp, VLKTIMEOUT, 0);
	if(error) {
		ntfs_frele(fp);
		ntfs_ntput(ip);
		return (error);
	}
	dprintf(("ntfs_vget: vnode: %p for ntnode: %ju\n", vp, (uintmax_t)ino));

	fp->f_vp = vp;
	vp->v_data = fp;
	vp->v_type = f_type;

	if (ino == NTFS_ROOTINO)
		vsetflags(vp, VROOT);

	/*
	 * Normal files use the buffer cache
	 */
	if (f_type == VREG)
		vinitvmio(vp, fp->f_size, PAGE_SIZE, -1);

	ntfs_ntput(ip);

	KKASSERT(lkflags & LK_TYPE_MASK);
	/* XXX leave vnode locked exclusively from getnewvnode */
	*vpp = vp;
	return (0);
	
}

static int
ntfs_vget(struct mount *mp, struct vnode *dvp, ino_t ino, struct vnode **vpp)
{
	return ntfs_vgetex(mp, ino, NTFS_A_DATA, NULL,
			LK_EXCLUSIVE | LK_RETRY, 0, curthread, vpp);
}

#if defined(__DragonFly__)
static struct vfsops ntfs_vfsops = {
	.vfs_mount =    	ntfs_mount,
	.vfs_unmount =   	ntfs_unmount,
	.vfs_root =     	ntfs_root,
	.vfs_statfs =   	ntfs_statfs,
	.vfs_statvfs =		ntfs_statvfs,
	.vfs_sync =      	vfs_stdsync,
	.vfs_vget =     	ntfs_vget,
	.vfs_fhtovp =   	ntfs_fhtovp,
	.vfs_checkexp =   	ntfs_checkexp,
	.vfs_vptofh =   	ntfs_vptofh,
	.vfs_init =     	ntfs_init,
	.vfs_uninit =   	ntfs_nthash_uninit /* see ntfs_ihash.c */
};
VFS_SET(ntfs_vfsops, ntfs, 0);
MODULE_VERSION(ntfs, 1);
#elif defined(__NetBSD__)
extern struct vnodeopv_desc ntfs_vnodeop_opv_desc;

struct vnodeopv_desc *ntfs_vnodeopv_descs[] = {
	&ntfs_vnodeop_opv_desc,
	NULL,
};

struct vfsops ntfs_vfsops = {
	MOUNT_NTFS,
	ntfs_mount,
	ntfs_start,
	ntfs_unmount,
	ntfs_root,
	ntfs_quotactl,
	ntfs_statfs,
	ntfs_sync,
	ntfs_vget,
	ntfs_fhtovp,
	ntfs_vptofh,
	ntfs_init,
	ntfs_sysctl,
	ntfs_mountroot,
	ntfs_checkexp,
	ntfs_vnodeopv_descs,
};
#else /* !NetBSD && !FreeBSD */
static struct vfsops ntfs_vfsops = {
	ntfs_mount,
	ntfs_start,
	ntfs_unmount,
	ntfs_root,
	ntfs_quotactl,
	ntfs_statfs,
	ntfs_sync,
	ntfs_vget,
	ntfs_fhtovp,
	ntfs_vptofh,
	ntfs_init,
};
VFS_SET(ntfs_vfsops, ntfs, MOUNT_NTFS, 0);
#endif


