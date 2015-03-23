/*
 * Copyright (c) 1989, 1993, 1995
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rick Macklem at The University of Guelph.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 *	@(#)nfs_vfsops.c	8.12 (Berkeley) 5/20/95
 * $FreeBSD: src/sys/nfs/nfs_vfsops.c,v 1.91.2.7 2003/01/27 20:04:08 dillon Exp $
 */

#include "opt_bootp.h"
#include "opt_nfsroot.h"

#include <sys/param.h>
#include <sys/sockio.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/systm.h>
#include <sys/objcache.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>

#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>

#include <sys/thread2.h>
#include <sys/mutex2.h>

#include "rpcv2.h"
#include "nfsproto.h"
#include "nfs.h"
#include "nfsmount.h"
#include "nfsnode.h"
#include "xdr_subs.h"
#include "nfsm_subs.h"
#include "nfsdiskless.h"
#include "nfsmountrpc.h"

extern int	nfs_mountroot(struct mount *mp);
extern void	bootpc_init(void);

extern struct vop_ops nfsv2_vnode_vops;
extern struct vop_ops nfsv2_fifo_vops;
extern struct vop_ops nfsv2_spec_vops;

MALLOC_DEFINE(M_NFSREQ, "NFS req", "NFS request header");
MALLOC_DEFINE(M_NFSBIGFH, "NFSV3 bigfh", "NFS version 3 file handle");
MALLOC_DEFINE(M_NFSD, "NFS daemon", "Nfs server daemon structure");
MALLOC_DEFINE(M_NFSDIROFF, "NFSV3 diroff", "NFS directory offset data");
MALLOC_DEFINE(M_NFSRVDESC, "NFSV3 srvdesc", "NFS server socket descriptor");
MALLOC_DEFINE(M_NFSUID, "NFS uid", "Nfs uid mapping structure");
MALLOC_DEFINE(M_NFSHASH, "NFS hash", "NFS hash tables");

struct objcache *nfsmount_objcache;

struct nfsstats	nfsstats;
SYSCTL_NODE(_vfs, OID_AUTO, nfs, CTLFLAG_RW, 0, "NFS filesystem");
SYSCTL_STRUCT(_vfs_nfs, NFS_NFSSTATS, nfsstats, CTLFLAG_RD, &nfsstats, nfsstats,
    "Nfs stats structure");
static int nfs_ip_paranoia = 1;
SYSCTL_INT(_vfs_nfs, OID_AUTO, nfs_ip_paranoia, CTLFLAG_RW, &nfs_ip_paranoia, 0,
    "Enable no-connection mode for protocols that support no-connection mode");
#ifdef NFS_DEBUG
int nfs_debug;
SYSCTL_INT(_vfs_nfs, OID_AUTO, debug, CTLFLAG_RW, &nfs_debug, 0, "");
#endif

/*
 * Tunable to determine the Read/Write unit size.  Maximum value
 * is NFS_MAXDATA.  We also default to NFS_MAXDATA.
 */
static int nfs_io_size = NFS_MAXDATA;
SYSCTL_INT(_vfs_nfs, OID_AUTO, nfs_io_size, CTLFLAG_RW,
	&nfs_io_size, 0, "NFS optimal I/O unit size");

static void	nfs_decode_args (struct nfsmount *nmp,
			struct nfs_args *argp);
static int	mountnfs (struct nfs_args *,struct mount *,
			struct sockaddr *,char *,char *,struct vnode **);
static int	nfs_mount ( struct mount *mp, char *path, caddr_t data,
			struct ucred *cred);
static int	nfs_unmount ( struct mount *mp, int mntflags);
static int	nfs_root ( struct mount *mp, struct vnode **vpp);
static int	nfs_statfs ( struct mount *mp, struct statfs *sbp,
			struct ucred *cred);
static int	nfs_statvfs(struct mount *mp, struct statvfs *sbp,
				struct ucred *cred);
static int	nfs_sync ( struct mount *mp, int waitfor);

/*
 * nfs vfs operations.
 */
static struct vfsops nfs_vfsops = {
	.vfs_mount =    	nfs_mount,
	.vfs_unmount =  	nfs_unmount,
	.vfs_root =     	nfs_root,
	.vfs_statfs =    	nfs_statfs,
	.vfs_statvfs =   	nfs_statvfs,
	.vfs_sync =     	nfs_sync,
	.vfs_init =     	nfs_init,
	.vfs_uninit =    	nfs_uninit
};
VFS_SET(nfs_vfsops, nfs, VFCF_NETWORK);
MODULE_VERSION(nfs, 1);

/*
 * This structure must be filled in by a primary bootstrap or bootstrap
 * server for a diskless/dataless machine. It is initialized below just
 * to ensure that it is allocated to initialized data (.data not .bss).
 */
struct nfs_diskless nfs_diskless = { { { 0 } } };
struct nfsv3_diskless nfsv3_diskless = { { { 0 } } };
int nfs_diskless_valid = 0;

SYSCTL_INT(_vfs_nfs, OID_AUTO, diskless_valid, CTLFLAG_RD,
	&nfs_diskless_valid, 0,
	"NFS diskless params were obtained");

SYSCTL_STRING(_vfs_nfs, OID_AUTO, diskless_rootpath, CTLFLAG_RD,
	nfsv3_diskless.root_hostnam, 0,
	"Host name for mount point");

SYSCTL_OPAQUE(_vfs_nfs, OID_AUTO, diskless_rootaddr, CTLFLAG_RD,
	&nfsv3_diskless.root_saddr, sizeof nfsv3_diskless.root_saddr,
	"%Ssockaddr_in", "Address of root server");

SYSCTL_STRING(_vfs_nfs, OID_AUTO, diskless_swappath, CTLFLAG_RD,
	nfsv3_diskless.swap_hostnam, 0,
	"Host name for mount ppoint");

SYSCTL_OPAQUE(_vfs_nfs, OID_AUTO, diskless_swapaddr, CTLFLAG_RD,
	&nfsv3_diskless.swap_saddr, sizeof nfsv3_diskless.swap_saddr,
	"%Ssockaddr_in", "Address of swap server");


void nfsargs_ntoh (struct nfs_args *);
static int nfs_mountdiskless (char *, char *, int,
				  struct sockaddr_in *, struct nfs_args *,
				  struct thread *, struct vnode **,
				  struct mount **);
static void nfs_convert_diskless (void);
static void nfs_convert_oargs (struct nfs_args *args,
				   struct onfs_args *oargs);

/*
 * Calculate the buffer I/O block size to use.  The maximum V2 block size
 * is typically 8K, the maximum datagram size is typically 16K, and the
 * maximum V3 block size is typically 32K.  The buffer cache tends to work
 * best with 16K blocks but we allow 32K for TCP connections.
 *
 * We force the block size to be at least a page for buffer cache efficiency.
 */
static int
nfs_iosize(int v3, int sotype)
{
	int iosize;
	int iomax;

	if (v3) {
		if (sotype == SOCK_STREAM)
			iomax = NFS_MAXDATA;
		else
			iomax = NFS_MAXDGRAMDATA;
	} else {
		iomax = NFS_V2MAXDATA;
	}
	if ((iosize = nfs_io_size) > iomax)
		iosize = iomax;
	if (iosize < PAGE_SIZE)
		iosize = PAGE_SIZE;

	/*
	 * This is an aweful hack but until the buffer cache is rewritten
	 * we need it.  The problem is that when you combine write() with
	 * mmap() the vm_page->valid bits can become weird looking
	 * (e.g. 0xfc).  This occurs because NFS uses piecemeal buffers
	 * at the file EOF.  To solve the problem the BIO system needs to
	 * be guarenteed that the NFS iosize for regular files will be a
	 * multiple of PAGE_SIZE so it can invalidate the whole page
	 * rather then just the piece of it owned by the buffer when
	 * NFS does vinvalbuf() calls.
	 */
	if (iosize & PAGE_MASK)
		iosize = (iosize & ~PAGE_MASK) + PAGE_SIZE;
	return iosize;
}

static void
nfs_convert_oargs(struct nfs_args *args, struct onfs_args *oargs)
{
	args->version = NFS_ARGSVERSION;
	args->addr = oargs->addr;
	args->addrlen = oargs->addrlen;
	args->sotype = oargs->sotype;
	args->proto = oargs->proto;
	args->fh = oargs->fh;
	args->fhsize = oargs->fhsize;
	args->flags = oargs->flags;
	args->wsize = oargs->wsize;
	args->rsize = oargs->rsize;
	args->readdirsize = oargs->readdirsize;
	args->timeo = oargs->timeo;
	args->retrans = oargs->retrans;
	args->maxgrouplist = oargs->maxgrouplist;
	args->readahead = oargs->readahead;
	args->deadthresh = oargs->deadthresh;
	args->hostname = oargs->hostname;
}

static void
nfs_convert_diskless(void)
{
	int i;

	bcopy(&nfs_diskless.myif, &nfsv3_diskless.myif,
		sizeof(struct ifaliasreq));
	bcopy(&nfs_diskless.mygateway, &nfsv3_diskless.mygateway,
		sizeof(struct sockaddr_in));
	nfs_convert_oargs(&nfsv3_diskless.swap_args, &nfs_diskless.swap_args);

	/*
	 * Copy the NFS handle passed from the diskless code.
	 *
	 * XXX CURRENTLY DISABLED - bootp passes us a NFSv2 handle which
	 * will fail utterly with HAMMER due to limitations with NFSv2
	 * directory cookies.
	 */
	bcopy(nfs_diskless.swap_fh, nfsv3_diskless.swap_fh, NFSX_V2FH);
	nfsv3_diskless.swap_fhsize = NFSX_V2FH;
	for (i = NFSX_V2FH - 1; i >= 0; --i) {
		if (nfs_diskless.swap_fh[i])
			break;
	}
	if (i < 0)
		nfsv3_diskless.swap_fhsize = 0;
	nfsv3_diskless.swap_fhsize = 0;		/* FORCE DISABLE */

	bcopy(&nfs_diskless.swap_saddr,&nfsv3_diskless.swap_saddr,
		sizeof(struct sockaddr_in));
	bcopy(nfs_diskless.swap_hostnam,nfsv3_diskless.swap_hostnam, MNAMELEN);
	nfsv3_diskless.swap_nblks = nfs_diskless.swap_nblks;
	bcopy(&nfs_diskless.swap_ucred, &nfsv3_diskless.swap_ucred,
		sizeof(struct ucred));
	nfs_convert_oargs(&nfsv3_diskless.root_args, &nfs_diskless.root_args);

	/*
	 * Copy the NFS handle passed from the diskless code.
	 *
	 * XXX CURRENTLY DISABLED - bootp passes us a NFSv2 handle which
	 * will fail utterly with HAMMER due to limitations with NFSv2
	 * directory cookies.
	 */
	bcopy(nfs_diskless.root_fh, nfsv3_diskless.root_fh, NFSX_V2FH);
	nfsv3_diskless.root_fhsize = NFSX_V2FH;
	for (i = NFSX_V2FH - 1; i >= 0; --i) {
		if (nfs_diskless.root_fh[i])
			break;
	}
	if (i < 0)
		nfsv3_diskless.root_fhsize = 0;
	nfsv3_diskless.root_fhsize = 0;		/* FORCE DISABLE */

	bcopy(&nfs_diskless.root_saddr,&nfsv3_diskless.root_saddr,
		sizeof(struct sockaddr_in));
	bcopy(nfs_diskless.root_hostnam,nfsv3_diskless.root_hostnam, MNAMELEN);
	nfsv3_diskless.root_time = nfs_diskless.root_time;
	bcopy(nfs_diskless.my_hostnam,nfsv3_diskless.my_hostnam,
		MAXHOSTNAMELEN);
	nfs_diskless_valid = 3;
}

/*
 * nfs statfs call
 */
int
nfs_statfs(struct mount *mp, struct statfs *sbp, struct ucred *cred)
{
	struct vnode *vp;
	struct nfs_statfs *sfp;
	struct nfsmount *nmp = VFSTONFS(mp);
	thread_t td = curthread;
	int error = 0, retattr;
	struct nfsnode *np;
	u_quad_t tquad;
	struct nfsm_info info;

	info.mrep = NULL;
	info.v3 = (nmp->nm_flag & NFSMNT_NFSV3);

	lwkt_gettoken(&nmp->nm_token);

#ifndef nolint
	sfp = NULL;
#endif
	error = nfs_nget(mp, (nfsfh_t *)nmp->nm_fh, nmp->nm_fhsize, &np, NULL);
	if (error) {
		lwkt_reltoken(&nmp->nm_token);
		return (error);
	}
	vp = NFSTOV(np);
	/* ignore the passed cred */
	cred = crget();
	cred->cr_ngroups = 1;
	if (info.v3 && (nmp->nm_state & NFSSTA_GOTFSINFO) == 0)
		(void)nfs_fsinfo(nmp, vp, td);
	nfsstats.rpccnt[NFSPROC_FSSTAT]++;
	nfsm_reqhead(&info, vp, NFSPROC_FSSTAT, NFSX_FH(info.v3));
	ERROROUT(nfsm_fhtom(&info, vp));
	NEGKEEPOUT(nfsm_request(&info, vp, NFSPROC_FSSTAT, td, cred, &error));
	if (info.v3) {
		ERROROUT(nfsm_postop_attr(&info, vp, &retattr,
					 NFS_LATTR_NOSHRINK));
	}
	if (error) {
		if (info.mrep != NULL)
			m_freem(info.mrep);
		goto nfsmout;
	}
	NULLOUT(sfp = nfsm_dissect(&info, NFSX_STATFS(info.v3)));
	sbp->f_flags = nmp->nm_flag;

	if (info.v3) {
		sbp->f_bsize = NFS_FABLKSIZE;
		tquad = fxdr_hyper(&sfp->sf_tbytes);
		sbp->f_blocks = (long)(tquad / ((u_quad_t)NFS_FABLKSIZE));
		tquad = fxdr_hyper(&sfp->sf_fbytes);
		sbp->f_bfree = (long)(tquad / ((u_quad_t)NFS_FABLKSIZE));
		tquad = fxdr_hyper(&sfp->sf_abytes);
		sbp->f_bavail = (long)(tquad / ((u_quad_t)NFS_FABLKSIZE));
		sbp->f_files = (fxdr_unsigned(int32_t,
		    sfp->sf_tfiles.nfsuquad[1]) & 0x7fffffff);
		sbp->f_ffree = (fxdr_unsigned(int32_t,
		    sfp->sf_ffiles.nfsuquad[1]) & 0x7fffffff);
	} else {
		sbp->f_bsize = fxdr_unsigned(int32_t, sfp->sf_bsize);
		sbp->f_blocks = fxdr_unsigned(int32_t, sfp->sf_blocks);
		sbp->f_bfree = fxdr_unsigned(int32_t, sfp->sf_bfree);
		sbp->f_bavail = fxdr_unsigned(int32_t, sfp->sf_bavail);
		sbp->f_files = 0;
		sbp->f_ffree = 0;
	}

	/*
	 * Some values are pre-set in mnt_stat.  Note in particular f_iosize
	 * cannot be changed once the filesystem is mounted as it is used
	 * as the basis for BIOs.
	 */
	if (sbp != &mp->mnt_stat) {
		sbp->f_type = mp->mnt_vfc->vfc_typenum;
		bcopy(mp->mnt_stat.f_mntfromname, sbp->f_mntfromname, MNAMELEN);
		sbp->f_iosize = mp->mnt_stat.f_iosize;
	}
	m_freem(info.mrep);
	info.mrep = NULL;
nfsmout:
	vput(vp);
	crfree(cred);
	lwkt_reltoken(&nmp->nm_token);
	return (error);
}

static int
nfs_statvfs(struct mount *mp, struct statvfs *sbp, struct ucred *cred)
{
	struct vnode *vp;
	struct nfs_statfs *sfp;
	struct nfsmount *nmp = VFSTONFS(mp);
	thread_t td = curthread;
	int error = 0, retattr;
	struct nfsnode *np;
	struct nfsm_info info;

	info.mrep = NULL;
	info.v3 = (nmp->nm_flag & NFSMNT_NFSV3);
	lwkt_gettoken(&nmp->nm_token);

#ifndef nolint
	sfp = NULL;
#endif
	error = nfs_nget(mp, (nfsfh_t *)nmp->nm_fh, nmp->nm_fhsize, &np, NULL);
	if (error) {
		lwkt_reltoken(&nmp->nm_token);
		return (error);
	}
	vp = NFSTOV(np);
	/* ignore the passed cred */
	cred = crget();
	cred->cr_ngroups = 1;
	if (info.v3 && (nmp->nm_state & NFSSTA_GOTFSINFO) == 0)
		(void)nfs_fsinfo(nmp, vp, td);
	nfsstats.rpccnt[NFSPROC_FSSTAT]++;
	nfsm_reqhead(&info, vp, NFSPROC_FSSTAT, NFSX_FH(info.v3));
	ERROROUT(nfsm_fhtom(&info, vp));
	NEGKEEPOUT(nfsm_request(&info, vp, NFSPROC_FSSTAT, td, cred, &error));
	if (info.v3) {
		ERROROUT(nfsm_postop_attr(&info, vp, &retattr,
					 NFS_LATTR_NOSHRINK));
	}
	if (error) {
		if (info.mrep != NULL)
			m_freem(info.mrep);
		goto nfsmout;
	}
	NULLOUT(sfp = nfsm_dissect(&info, NFSX_STATFS(info.v3)));
	sbp->f_flag = nmp->nm_flag;
	sbp->f_owner = nmp->nm_cred->cr_ruid;

	if (info.v3) {
		sbp->f_bsize = NFS_FABLKSIZE;
		sbp->f_frsize = NFS_FABLKSIZE;
		sbp->f_blocks = (fxdr_hyper(&sfp->sf_tbytes) /
				((u_quad_t)NFS_FABLKSIZE));
		sbp->f_bfree = (fxdr_hyper(&sfp->sf_fbytes) /
				((u_quad_t)NFS_FABLKSIZE));
		sbp->f_bavail = (fxdr_hyper(&sfp->sf_abytes) /
				((u_quad_t)NFS_FABLKSIZE));
		sbp->f_files = fxdr_hyper(&sfp->sf_tfiles);
		sbp->f_ffree = fxdr_hyper(&sfp->sf_ffiles);
		sbp->f_favail = fxdr_hyper(&sfp->sf_afiles);
	} else {
		sbp->f_bsize = fxdr_unsigned(int32_t, sfp->sf_bsize);
		sbp->f_blocks = fxdr_unsigned(int32_t, sfp->sf_blocks);
		sbp->f_bfree = fxdr_unsigned(int32_t, sfp->sf_bfree);
		sbp->f_bavail = fxdr_unsigned(int32_t, sfp->sf_bavail);
		sbp->f_files = 0;
		sbp->f_ffree = 0;
		sbp->f_favail = 0;
	}
	sbp->f_syncreads = 0;
	sbp->f_syncwrites = 0;
	sbp->f_asyncreads = 0;
	sbp->f_asyncwrites = 0;
	sbp->f_type = mp->mnt_vfc->vfc_typenum;

	m_freem(info.mrep);
	info.mrep = NULL;
nfsmout:
	vput(vp);
	crfree(cred);
	lwkt_reltoken(&nmp->nm_token);
	return (error);
}

/*
 * nfs version 3 fsinfo rpc call
 */
int
nfs_fsinfo(struct nfsmount *nmp, struct vnode *vp, struct thread *td)
{
	struct nfsv3_fsinfo *fsp;
	u_int32_t pref, max;
	int error = 0, retattr;
	u_int64_t maxfsize;
	struct nfsm_info info;

	info.v3 = 1;
	nfsstats.rpccnt[NFSPROC_FSINFO]++;
	nfsm_reqhead(&info, vp, NFSPROC_FSINFO, NFSX_FH(1));
	ERROROUT(nfsm_fhtom(&info, vp));
	NEGKEEPOUT(nfsm_request(&info, vp, NFSPROC_FSINFO, td,
				nfs_vpcred(vp, ND_READ), &error));
	ERROROUT(nfsm_postop_attr(&info, vp, &retattr, NFS_LATTR_NOSHRINK));
	if (error == 0) {
		NULLOUT(fsp = nfsm_dissect(&info, NFSX_V3FSINFO));
		pref = fxdr_unsigned(u_int32_t, fsp->fs_wtpref);
		if (pref < nmp->nm_wsize && pref >= NFS_FABLKSIZE)
			nmp->nm_wsize = (pref + NFS_FABLKSIZE - 1) &
				~(NFS_FABLKSIZE - 1);
		max = fxdr_unsigned(u_int32_t, fsp->fs_wtmax);
		if (max < nmp->nm_wsize && max > 0) {
			nmp->nm_wsize = max & ~(NFS_FABLKSIZE - 1);
			if (nmp->nm_wsize == 0)
				nmp->nm_wsize = max;
		}
		pref = fxdr_unsigned(u_int32_t, fsp->fs_rtpref);
		if (pref < nmp->nm_rsize && pref >= NFS_FABLKSIZE)
			nmp->nm_rsize = (pref + NFS_FABLKSIZE - 1) &
				~(NFS_FABLKSIZE - 1);
		max = fxdr_unsigned(u_int32_t, fsp->fs_rtmax);
		if (max < nmp->nm_rsize && max > 0) {
			nmp->nm_rsize = max & ~(NFS_FABLKSIZE - 1);
			if (nmp->nm_rsize == 0)
				nmp->nm_rsize = max;
		}
		pref = fxdr_unsigned(u_int32_t, fsp->fs_dtpref);
		if (pref < nmp->nm_readdirsize && pref >= NFS_DIRBLKSIZ)
			nmp->nm_readdirsize = (pref + NFS_DIRBLKSIZ - 1) &
				~(NFS_DIRBLKSIZ - 1);
		if (max < nmp->nm_readdirsize && max > 0) {
			nmp->nm_readdirsize = max & ~(NFS_DIRBLKSIZ - 1);
			if (nmp->nm_readdirsize == 0)
				nmp->nm_readdirsize = max;
		}
		maxfsize = fxdr_hyper(&fsp->fs_maxfilesize);
		if (maxfsize > 0 && maxfsize < nmp->nm_maxfilesize)
			nmp->nm_maxfilesize = maxfsize;
		nmp->nm_state |= NFSSTA_GOTFSINFO;

		/*
		 * Use the smaller of rsize/wsize for the biosize.
		 */
		if (nmp->nm_rsize < nmp->nm_wsize)
			nmp->nm_mountp->mnt_stat.f_iosize = nmp->nm_rsize;
		else
			nmp->nm_mountp->mnt_stat.f_iosize = nmp->nm_wsize;
	}
	m_freem(info.mrep);
	info.mrep = NULL;
nfsmout:
	return (error);
}

/*
 * Mount a remote root fs via. nfs. This depends on the info in the
 * nfs_diskless structure that has been filled in properly by some primary
 * bootstrap.
 * It goes something like this:
 * - do enough of "ifconfig" by calling ifioctl() so that the system
 *   can talk to the server
 * - If nfs_diskless.mygateway is filled in, use that address as
 *   a default gateway.
 * - build the rootfs mount point and call mountnfs() to do the rest.
 */
int
nfs_mountroot(struct mount *mp)
{
	struct mount  *swap_mp;
	struct nfsv3_diskless *nd = &nfsv3_diskless;
	struct socket *so;
	struct vnode *vp;
	struct thread *td = curthread;		/* XXX */
	int error, i;
	u_long l;
	char buf[128];

#if defined(BOOTP_NFSROOT) && defined(BOOTP)
	bootpc_init();		/* use bootp to get nfs_diskless filled in */
#endif

	/*
	 * XXX time must be non-zero when we init the interface or else
	 * the arp code will wedge...
	 */
	while (mycpu->gd_time_seconds == 0)
		tsleep(mycpu, 0, "arpkludge", 10);

	/*
	 * The boot code may have passed us a diskless structure.
	 */
	kprintf("DISKLESS %d\n", nfs_diskless_valid);
	if (nfs_diskless_valid == 1)
		nfs_convert_diskless();

	/*
	 * NFSv3 is required.
	 */
	nd->root_args.flags |= NFSMNT_NFSV3 | NFSMNT_RDIRPLUS;
	nd->swap_args.flags |= NFSMNT_NFSV3;

#define SINP(sockaddr)	((struct sockaddr_in *)(sockaddr))
	kprintf("nfs_mountroot: interface %s ip %s",
		nd->myif.ifra_name,
		inet_ntoa(SINP(&nd->myif.ifra_addr)->sin_addr));
	kprintf(" bcast %s",
		inet_ntoa(SINP(&nd->myif.ifra_broadaddr)->sin_addr));
	kprintf(" mask %s\n",
		inet_ntoa(SINP(&nd->myif.ifra_mask)->sin_addr));
#undef SINP

	/*
	 * XXX splnet, so networks will receive...
	 */
	crit_enter();

	/*
	 * BOOTP does not necessarily have to be compiled into the kernel
	 * for an NFS root to work.  If we inherited the network
	 * configuration for PXEBOOT then pxe_setup_nfsdiskless() has figured
	 * out our interface for us and all we need to do is ifconfig the
	 * interface.  We only do this if the interface has not already been
	 * ifconfig'd by e.g. BOOTP.
	 */
	error = socreate(nd->myif.ifra_addr.sa_family, &so, SOCK_DGRAM, 0, td);
	if (error) {
		panic("nfs_mountroot: socreate(%04x): %d",
			nd->myif.ifra_addr.sa_family, error);
	}

	error = ifioctl(so, SIOCAIFADDR, (caddr_t)&nd->myif, proc0.p_ucred);
	if (error)
		panic("nfs_mountroot: SIOCAIFADDR: %d", error);

	soclose(so, FNONBLOCK);

	/*
	 * If the gateway field is filled in, set it as the default route.
	 */
	if (nd->mygateway.sin_len != 0) {
		struct sockaddr_in mask, sin;

		bzero((caddr_t)&mask, sizeof(mask));
		sin = mask;
		sin.sin_family = AF_INET;
		sin.sin_len = sizeof(sin);
		kprintf("nfs_mountroot: gateway %s\n",
			inet_ntoa(nd->mygateway.sin_addr));
		error = rtrequest_global(RTM_ADD, (struct sockaddr *)&sin,
					(struct sockaddr *)&nd->mygateway,
					(struct sockaddr *)&mask,
					RTF_UP | RTF_GATEWAY);
		if (error)
			kprintf("nfs_mountroot: unable to set gateway, error %d, continuing anyway\n", error);
	}

	/*
	 * Create the rootfs mount point.
	 */
	nd->root_args.fh = nd->root_fh;
	nd->root_args.fhsize = nd->root_fhsize;
	l = ntohl(nd->root_saddr.sin_addr.s_addr);
	ksnprintf(buf, sizeof(buf), "%ld.%ld.%ld.%ld:%s",
		(l >> 24) & 0xff, (l >> 16) & 0xff,
		(l >>  8) & 0xff, (l >>  0) & 0xff,nd->root_hostnam);
	kprintf("NFS_ROOT: %s\n",buf);
	error = nfs_mountdiskless(buf, "/", MNT_RDONLY, &nd->root_saddr,
				  &nd->root_args, td, &vp, &mp);
	if (error) {
		mp->mnt_vfc->vfc_refcount--;
		crit_exit();
		return (error);
	}

	swap_mp = NULL;
	if (nd->swap_nblks) {

		/* Convert to DEV_BSIZE instead of Kilobyte */
		nd->swap_nblks *= 2;

		/*
		 * Create a fake mount point just for the swap vnode so that the
		 * swap file can be on a different server from the rootfs.
		 */
		nd->swap_args.fh = nd->swap_fh;
		nd->swap_args.fhsize = nd->swap_fhsize;
		l = ntohl(nd->swap_saddr.sin_addr.s_addr);
		ksnprintf(buf, sizeof(buf), "%ld.%ld.%ld.%ld:%s",
			(l >> 24) & 0xff, (l >> 16) & 0xff,
			(l >>  8) & 0xff, (l >>  0) & 0xff,nd->swap_hostnam);
		kprintf("NFS SWAP: %s\n",buf);
		error = nfs_mountdiskless(buf, "/swap", 0, &nd->swap_saddr,
					  &nd->swap_args, td, &vp, &swap_mp);
		if (error) {
			crit_exit();
			return (error);
		}
		vfs_unbusy(swap_mp);

		VTONFS(vp)->n_size = VTONFS(vp)->n_vattr.va_size =
				nd->swap_nblks * DEV_BSIZE ;

		/*
		 * Since the swap file is not the root dir of a file system,
		 * hack it to a regular file.
		 */
		vclrflags(vp, VROOT);
		vref(vp);
		nfs_setvtype(vp, VREG);
		swaponvp(td, vp, nd->swap_nblks);
	}

	mp->mnt_flag |= MNT_ROOTFS;

	/*
	 * This is not really an nfs issue, but it is much easier to
	 * set hostname here and then let the "/etc/rc.xxx" files
	 * mount the right /var based upon its preset value.
	 */
	bcopy(nd->my_hostnam, hostname, MAXHOSTNAMELEN);
	hostname[MAXHOSTNAMELEN - 1] = '\0';
	for (i = 0; i < MAXHOSTNAMELEN; i++)
		if (hostname[i] == '\0')
			break;
	inittodr(ntohl(nd->root_time));
	crit_exit();
	return (0);
}

/*
 * Internal version of mount system call for diskless setup.
 */
static int
nfs_mountdiskless(char *path, char *which, int mountflag,
	struct sockaddr_in *sin, struct nfs_args *args, struct thread *td,
	struct vnode **vpp, struct mount **mpp)
{
	struct mount *mp;
	struct sockaddr *nam;
	int didalloc = 0;
	int error;

	mp = *mpp;

	if (mp == NULL) {
		if ((error = vfs_rootmountalloc("nfs", path, &mp)) != 0) {
			kprintf("nfs_mountroot: NFS not configured");
			return (error);
		}
		didalloc = 1;
	}
	mp->mnt_kern_flag = 0;
	mp->mnt_flag = mountflag;
	nam = dup_sockaddr((struct sockaddr *)sin);

#if defined(BOOTP) || defined(NFS_ROOT)
	if (args->fhsize == 0) {
		char *xpath = path;

		kprintf("NFS_ROOT: No FH passed from loader, attempting "
			"mount rpc...");
		while (*xpath && *xpath != ':')
			++xpath;
		if (*xpath)
			++xpath;
		args->fhsize = 0;
		error = md_mount(sin, xpath, args->fh, &args->fhsize, args, td);
		if (error) {
			kprintf("failed error %d.\n", error);
			goto haderror;
		}
		kprintf("success!\n");
	}
#endif

	if ((error = mountnfs(args, mp, nam, which, path, vpp)) != 0) {
#if defined(BOOTP) || defined(NFS_ROOT)
haderror:
#endif
		kprintf("nfs_mountroot: mount %s on %s: %d", path, which, error);
		mp->mnt_vfc->vfc_refcount--;
		if (didalloc)
			kfree(mp, M_MOUNT);
		kfree(nam, M_SONAME);
		return (error);
	}
	*mpp = mp;
	return (0);
}

static void
nfs_decode_args(struct nfsmount *nmp, struct nfs_args *argp)
{
	int adjsock;
	int maxio;

	crit_enter();
	/*
	 * Silently clear NFSMNT_NOCONN if it's a TCP mount, it makes
	 * no sense in that context.
	 */
	if (nmp->nm_sotype == SOCK_STREAM) {
		nmp->nm_flag &= ~NFSMNT_NOCONN;
		argp->flags &= ~NFSMNT_NOCONN;
	}

	/*
	 * readdirplus is NFSv3 only.
	 */
	if ((argp->flags & NFSMNT_NFSV3) == 0) {
		nmp->nm_flag &= ~NFSMNT_RDIRPLUS;
		argp->flags &= ~NFSMNT_RDIRPLUS;
	}

	/*
	 * Re-bind if rsrvd port flag has changed
	 */
	adjsock = (nmp->nm_flag & NFSMNT_RESVPORT) !=
		  (argp->flags & NFSMNT_RESVPORT);

	/* Update flags atomically.  Don't change the lock bits. */
	nmp->nm_flag = argp->flags | nmp->nm_flag;
	crit_exit();

	if ((argp->flags & NFSMNT_TIMEO) && argp->timeo > 0) {
		nmp->nm_timeo = (argp->timeo * NFS_HZ + 5) / 10;
		if (nmp->nm_timeo < NFS_MINTIMEO)
			nmp->nm_timeo = NFS_MINTIMEO;
		else if (nmp->nm_timeo > NFS_MAXTIMEO)
			nmp->nm_timeo = NFS_MAXTIMEO;
	}

	if ((argp->flags & NFSMNT_RETRANS) && argp->retrans > 1) {
		nmp->nm_retry = argp->retrans;
		if (nmp->nm_retry > NFS_MAXREXMIT)
			nmp->nm_retry = NFS_MAXREXMIT;
	}

	/*
	 * These parameters effect the buffer cache and cannot be changed
	 * once we've successfully mounted.
	 */
	if ((nmp->nm_state & NFSSTA_GOTFSINFO) == 0) {
		maxio = nfs_iosize(argp->flags & NFSMNT_NFSV3, nmp->nm_sotype);

		if ((argp->flags & NFSMNT_WSIZE) && argp->wsize > 0) {
			nmp->nm_wsize = argp->wsize;
			/* Round down to multiple of blocksize */
			nmp->nm_wsize &= ~(NFS_FABLKSIZE - 1);
			if (nmp->nm_wsize <= 0)
				nmp->nm_wsize = NFS_FABLKSIZE;
		}
		if (nmp->nm_wsize > maxio)
			nmp->nm_wsize = maxio;
		if (nmp->nm_wsize > MAXBSIZE)
			nmp->nm_wsize = MAXBSIZE;

		if ((argp->flags & NFSMNT_RSIZE) && argp->rsize > 0) {
			nmp->nm_rsize = argp->rsize;
			/* Round down to multiple of blocksize */
			nmp->nm_rsize &= ~(NFS_FABLKSIZE - 1);
			if (nmp->nm_rsize <= 0)
				nmp->nm_rsize = NFS_FABLKSIZE;
		}
		if (nmp->nm_rsize > maxio)
			nmp->nm_rsize = maxio;
		if (nmp->nm_rsize > MAXBSIZE)
			nmp->nm_rsize = MAXBSIZE;

		if ((argp->flags & NFSMNT_READDIRSIZE) &&
		    argp->readdirsize > 0) {
			nmp->nm_readdirsize = argp->readdirsize;
		}
		if (nmp->nm_readdirsize > maxio)
			nmp->nm_readdirsize = maxio;
		if (nmp->nm_readdirsize > nmp->nm_rsize)
			nmp->nm_readdirsize = nmp->nm_rsize;
	}

	if ((argp->flags & NFSMNT_ACREGMIN) && argp->acregmin >= 0)
		nmp->nm_acregmin = argp->acregmin;
	else
		nmp->nm_acregmin = NFS_MINATTRTIMO;
	if ((argp->flags & NFSMNT_ACREGMAX) && argp->acregmax >= 0)
		nmp->nm_acregmax = argp->acregmax;
	else
		nmp->nm_acregmax = NFS_MAXATTRTIMO;
	if ((argp->flags & NFSMNT_ACDIRMIN) && argp->acdirmin >= 0)
		nmp->nm_acdirmin = argp->acdirmin;
	else
		nmp->nm_acdirmin = NFS_MINDIRATTRTIMO;
	if ((argp->flags & NFSMNT_ACDIRMAX) && argp->acdirmax >= 0)
		nmp->nm_acdirmax = argp->acdirmax;
	else
		nmp->nm_acdirmax = NFS_MAXDIRATTRTIMO;
	if (nmp->nm_acdirmin > nmp->nm_acdirmax)
		nmp->nm_acdirmin = nmp->nm_acdirmax;
	if (nmp->nm_acregmin > nmp->nm_acregmax)
		nmp->nm_acregmin = nmp->nm_acregmax;

	if ((argp->flags & NFSMNT_MAXGRPS) && argp->maxgrouplist >= 0) {
		if (argp->maxgrouplist <= NFS_MAXGRPS)
			nmp->nm_numgrps = argp->maxgrouplist;
		else
			nmp->nm_numgrps = NFS_MAXGRPS;
	}
	if ((argp->flags & NFSMNT_READAHEAD) && argp->readahead >= 0) {
		if (argp->readahead <= NFS_MAXRAHEAD)
			nmp->nm_readahead = argp->readahead;
		else
			nmp->nm_readahead = NFS_MAXRAHEAD;
	}
	if ((argp->flags & NFSMNT_DEADTHRESH) && argp->deadthresh >= 1) {
		if (argp->deadthresh <= NFS_NEVERDEAD)
			nmp->nm_deadthresh = argp->deadthresh;
		else
			nmp->nm_deadthresh = NFS_NEVERDEAD;
	}

	if (nmp->nm_so && adjsock) {
		nfs_safedisconnect(nmp);
		if (nmp->nm_sotype == SOCK_DGRAM)
			while (nfs_connect(nmp, NULL)) {
				kprintf("nfs_args: retrying connect\n");
				(void) tsleep((caddr_t)&lbolt, 0, "nfscon", 0);
			}
	}
}

/*
 * VFS Operations.
 *
 * mount system call
 * It seems a bit dumb to copyinstr() the host and path here and then
 * bcopy() them in mountnfs(), but I wanted to detect errors before
 * doing the sockargs() call because sockargs() allocates an mbuf and
 * an error after that means that I have to release the mbuf.
 */
/* ARGSUSED */
static int
nfs_mount(struct mount *mp, char *path, caddr_t data, struct ucred *cred)
{
	int error;
	struct nfs_args args;
	struct sockaddr *nam;
	struct vnode *vp;
	char pth[MNAMELEN], hst[MNAMELEN];
	size_t len;
	u_char nfh[NFSX_V3FHMAX];

	if (path == NULL) {
		nfs_mountroot(mp);
		return (0);
	}
	error = copyin(data, (caddr_t)&args, sizeof (struct nfs_args));
	if (error)
		return (error);
	if (args.version != NFS_ARGSVERSION) {
#ifdef COMPAT_PRELITE2
		/*
		 * If the argument version is unknown, then assume the
		 * caller is a pre-lite2 4.4BSD client and convert its
		 * arguments.
		 */
		struct onfs_args oargs;
		error = copyin(data, (caddr_t)&oargs, sizeof (struct onfs_args));
		if (error)
			return (error);
		nfs_convert_oargs(&args,&oargs);
#else /* !COMPAT_PRELITE2 */
		return (EPROGMISMATCH);
#endif /* COMPAT_PRELITE2 */
	}
	if (mp->mnt_flag & MNT_UPDATE) {
		struct nfsmount *nmp = VFSTONFS(mp);

		if (nmp == NULL)
			return (EIO);
		/*
		 * When doing an update, we can't change from or to
		 * v3, or change cookie translation, or rsize or wsize.
		 */
		args.flags &= ~(NFSMNT_NFSV3 | NFSMNT_RSIZE | NFSMNT_WSIZE);
		args.flags |= nmp->nm_flag & (NFSMNT_NFSV3);
		nfs_decode_args(nmp, &args);
		return (0);
	}

	/*
	 * Make the nfs_ip_paranoia sysctl serve as the default connection
	 * or no-connection mode for those protocols that support
	 * no-connection mode (the flag will be cleared later for protocols
	 * that do not support no-connection mode).  This will allow a client
	 * to receive replies from a different IP then the request was
	 * sent to.  Note: default value for nfs_ip_paranoia is 1 (paranoid),
	 * not 0.
	 */
	if (nfs_ip_paranoia == 0)
		args.flags |= NFSMNT_NOCONN;
	if (args.fhsize < 0 || args.fhsize > NFSX_V3FHMAX)
		return (EINVAL);
	error = copyin((caddr_t)args.fh, (caddr_t)nfh, args.fhsize);
	if (error)
		return (error);
	error = copyinstr(path, pth, MNAMELEN-1, &len);
	if (error)
		return (error);
	bzero(&pth[len], MNAMELEN - len);
	error = copyinstr(args.hostname, hst, MNAMELEN-1, &len);
	if (error)
		return (error);
	bzero(&hst[len], MNAMELEN - len);
	/* sockargs() call must be after above copyin() calls */
	error = getsockaddr(&nam, (caddr_t)args.addr, args.addrlen);
	if (error)
		return (error);
	args.fh = nfh;
	error = mountnfs(&args, mp, nam, pth, hst, &vp);
	return (error);
}

/*
 * Common code for mount and mountroot
 */
static int
mountnfs(struct nfs_args *argp, struct mount *mp, struct sockaddr *nam,
	char *pth, char *hst, struct vnode **vpp)
{
	struct nfsmount *nmp;
	struct nfsnode *np;
	int error;
	int rxcpu;
	int txcpu;

	if (mp->mnt_flag & MNT_UPDATE) {
		nmp = VFSTONFS(mp);
		/* update paths, file handles, etc, here	XXX */
		kfree(nam, M_SONAME);
		return (0);
	} else {
		nmp = objcache_get(nfsmount_objcache, M_WAITOK);
		bzero((caddr_t)nmp, sizeof (struct nfsmount));
		mtx_init(&nmp->nm_rxlock, "nfsrx");
		mtx_init(&nmp->nm_txlock, "nfstx");
		TAILQ_INIT(&nmp->nm_uidlruhead);
		TAILQ_INIT(&nmp->nm_bioq);
		TAILQ_INIT(&nmp->nm_reqq);
		TAILQ_INIT(&nmp->nm_reqtxq);
		TAILQ_INIT(&nmp->nm_reqrxq);
		mp->mnt_data = (qaddr_t)nmp;
		lwkt_token_init(&nmp->nm_token, "nfs_token");
	}
	vfs_getnewfsid(mp);
	nmp->nm_mountp = mp;
	mp->mnt_kern_flag |= MNTK_ALL_MPSAFE;
	mp->mnt_kern_flag |= MNTK_THR_SYNC;	/* new vsyncscan semantics */

	lwkt_gettoken(&nmp->nm_token);

	/*
	 * V2 can only handle 32 bit filesizes.  A 4GB-1 limit may be too
	 * high, depending on whether we end up with negative offsets in
	 * the client or server somewhere.  2GB-1 may be safer.
	 *
	 * For V3, nfs_fsinfo will adjust this as necessary.  Assume maximum
	 * that we can handle until we find out otherwise.  Note that seek
	 * offsets are signed.
	 */
	if ((argp->flags & NFSMNT_NFSV3) == 0)
		nmp->nm_maxfilesize = 0xffffffffLL;
	else
		nmp->nm_maxfilesize = 0x7fffffffffffffffLL;

	nmp->nm_timeo = NFS_TIMEO;
	nmp->nm_retry = NFS_RETRANS;
	nmp->nm_wsize = nfs_iosize(argp->flags & NFSMNT_NFSV3, argp->sotype);
	nmp->nm_rsize = nmp->nm_wsize;
	nmp->nm_readdirsize = NFS_READDIRSIZE;
	nmp->nm_numgrps = NFS_MAXGRPS;
	nmp->nm_readahead = NFS_DEFRAHEAD;
	nmp->nm_deadthresh = NFS_DEADTHRESH;
	nmp->nm_fhsize = argp->fhsize;
	bcopy((caddr_t)argp->fh, (caddr_t)nmp->nm_fh, argp->fhsize);
	bcopy(hst, mp->mnt_stat.f_mntfromname, MNAMELEN);
	nmp->nm_nam = nam;
	/* Set up the sockets and per-host congestion */
	nmp->nm_sotype = argp->sotype;
	nmp->nm_soproto = argp->proto;
	nmp->nm_cred = crhold(proc0.p_ucred);

	nfs_decode_args(nmp, argp);

	/*
	 * For Connection based sockets (TCP,...) defer the connect until
	 * the first request, in case the server is not responding.
	 */
	if (nmp->nm_sotype == SOCK_DGRAM &&
		(error = nfs_connect(nmp, NULL)))
		goto bad;

	/*
	 * This is silly, but it has to be set so that vinifod() works.
	 * We do not want to do an nfs_statfs() here since we can get
	 * stuck on a dead server and we are holding a lock on the mount
	 * point.
	 */
	mp->mnt_stat.f_iosize =
		nfs_iosize(nmp->nm_flag & NFSMNT_NFSV3, nmp->nm_sotype);

	/*
	 * Install vop_ops for our vnops
	 */
	vfs_add_vnodeops(mp, &nfsv2_vnode_vops, &mp->mnt_vn_norm_ops);
	vfs_add_vnodeops(mp, &nfsv2_spec_vops, &mp->mnt_vn_spec_ops);
	vfs_add_vnodeops(mp, &nfsv2_fifo_vops, &mp->mnt_vn_fifo_ops);

	/*
	 * A reference count is needed on the nfsnode representing the
	 * remote root.  If this object is not persistent, then backward
	 * traversals of the mount point (i.e. "..") will not work if
	 * the nfsnode gets flushed out of the cache. Ufs does not have
	 * this problem, because one can identify root inodes by their
	 * number == ROOTINO (2).
	 */
	error = nfs_nget(mp, (nfsfh_t *)nmp->nm_fh, nmp->nm_fhsize, &np, NULL);
	if (error)
		goto bad;
	*vpp = NFSTOV(np);

	/*
	 * Retrieval of mountpoint attributes is delayed until nfs_rot
	 * or nfs_statfs are first called.  This will happen either when
	 * we first traverse the mount point or if somebody does a df(1).
	 *
	 * NFSSTA_GOTFSINFO is used to flag if we have successfully
	 * retrieved mountpoint attributes.  In the case of NFSv3 we
	 * also flag static fsinfo.
	 */
	if (*vpp != NULL)
		(*vpp)->v_type = VNON;

	/*
	 * Lose the lock but keep the ref.
	 */
	vn_unlock(*vpp);
	lwkt_gettoken(&nfs_token);
	TAILQ_INSERT_TAIL(&nfs_mountq, nmp, nm_entry);
	lwkt_reltoken(&nfs_token);

	switch(ncpus) {
	case 0:
	case 1:
		rxcpu = 0;
		txcpu = 0;
		break;
	case 2:
		rxcpu = 0;
		txcpu = 1;
		break;
	default:
		rxcpu = -1;
		txcpu = -1;
		break;
	}

	/*
	 * Start the reader and writer threads.
	 */
	lwkt_create(nfssvc_iod_reader, nmp, &nmp->nm_rxthread,
		    NULL, 0, rxcpu, "nfsiod_rx");
	lwkt_create(nfssvc_iod_writer, nmp, &nmp->nm_txthread,
		    NULL, 0, txcpu, "nfsiod_tx");
	lwkt_reltoken(&nmp->nm_token);
	return (0);
bad:
	nfs_disconnect(nmp);
	lwkt_reltoken(&nmp->nm_token);
	nfs_free_mount(nmp);
	return (error);
}

/*
 * unmount system call
 */
static int
nfs_unmount(struct mount *mp, int mntflags)
{
	struct nfsmount *nmp;
	int error, flags = 0;

	nmp = VFSTONFS(mp);
	lwkt_gettoken(&nmp->nm_token);
	if (mntflags & MNT_FORCE) {
		flags |= FORCECLOSE;
		nmp->nm_flag |= NFSMNT_FORCE;
	}

	/*
	 * Goes something like this..
	 * - Call vflush() to clear out vnodes for this file system
	 * - Close the socket
	 * - Free up the data structures
	 */
	/* In the forced case, cancel any outstanding requests. */
	if (flags & FORCECLOSE) {
		error = nfs_nmcancelreqs(nmp);
		if (error) {
			kprintf("NFS: %s: Unable to cancel all requests\n",
				mp->mnt_stat.f_mntfromname);
			/* continue anyway */
		}
	}

	/*
	 * Must handshake with nfs_clientd() if it is active. XXX
	 */
	nmp->nm_state |= NFSSTA_DISMINPROG;

	/*
	 * We hold 1 extra ref on the root vnode; see comment in mountnfs().
	 *
	 * If this doesn't work and we are doing a forced unmount we continue
	 * anyway.
	 */
	error = vflush(mp, 1, flags);
	if (error) {
		nmp->nm_state &= ~NFSSTA_DISMINPROG;
		if ((flags & FORCECLOSE) == 0) {
			lwkt_reltoken(&nmp->nm_token);
			return (error);
		}
	}

	/*
	 * We are now committed to the unmount.
	 * For NQNFS, let the server daemon free the nfsmount structure.
	 */
	if (nmp->nm_flag & NFSMNT_KERB)
		nmp->nm_state |= NFSSTA_DISMNT;
	nfssvc_iod_stop1(nmp);
	nfs_disconnect(nmp);
	nfssvc_iod_stop2(nmp);

	lwkt_gettoken(&nfs_token);
	TAILQ_REMOVE(&nfs_mountq, nmp, nm_entry);
	lwkt_reltoken(&nfs_token);

	lwkt_reltoken(&nmp->nm_token);

	if ((nmp->nm_flag & NFSMNT_KERB) == 0) {
		nfs_free_mount(nmp);
	}
	return (0);
}

void
nfs_free_mount(struct nfsmount *nmp)
{
	if (nmp->nm_cred)  {
		crfree(nmp->nm_cred);
		nmp->nm_cred = NULL;
	}
	if (nmp->nm_nam) {
		kfree(nmp->nm_nam, M_SONAME);
		nmp->nm_nam = NULL;
	}
	objcache_put(nfsmount_objcache, nmp);
}

/*
 * Return root of a filesystem
 */
static int
nfs_root(struct mount *mp, struct vnode **vpp)
{
	struct vnode *vp;
	struct nfsmount *nmp;
	struct vattr attrs;
	struct nfsnode *np;
	int error;

	nmp = VFSTONFS(mp);
	lwkt_gettoken(&nmp->nm_token);
	error = nfs_nget(mp, (nfsfh_t *)nmp->nm_fh, nmp->nm_fhsize, &np, NULL);
	if (error) {
		lwkt_reltoken(&nmp->nm_token);
		return (error);
	}
	vp = NFSTOV(np);

	/*
	 * Get transfer parameters and root vnode attributes
	 *
	 * NOTE: nfs_fsinfo() is expected to override the default
	 *	 f_iosize we set.
	 */
	if ((nmp->nm_state & NFSSTA_GOTFSINFO) == 0) {
	    if (nmp->nm_flag & NFSMNT_NFSV3) {
		mp->mnt_stat.f_iosize = nfs_iosize(1, nmp->nm_sotype);
		error = nfs_fsinfo(nmp, vp, curthread);
	    } else {
		if ((error = VOP_GETATTR(vp, &attrs)) == 0)
			nmp->nm_state |= NFSSTA_GOTFSINFO;

	    }
	} else {
	    /*
	     * The root vnode is usually cached by the namecache so do not
	     * try to avoid going over the wire even if we have previous
	     * information cached.  A stale NFS mount can loop
	     * forever resolving the root vnode if we return no-error when
	     * there is in fact an error.
	     */
	    np->n_attrstamp = 0;
	    error = VOP_GETATTR(vp, &attrs);
	}
	if (vp->v_type == VNON)
	    nfs_setvtype(vp, VDIR);
	vsetflags(vp, VROOT);
	if (error)
		vput(vp);
	else
		*vpp = vp;
	lwkt_reltoken(&nmp->nm_token);
	return (error);
}

struct scaninfo {
	int rescan;
	int waitfor;
	int allerror;
};

static int nfs_sync_scan2(struct mount *mp, struct vnode *vp, void *data);

/*
 * Flush out the buffer cache
 */
/* ARGSUSED */
static int
nfs_sync(struct mount *mp, int waitfor)
{
	struct nfsmount *nmp = VFSTONFS(mp);
	struct scaninfo scaninfo;
	int error;

	scaninfo.rescan = 1;
	scaninfo.waitfor = waitfor;
	scaninfo.allerror = 0;

	/*
	 * Force stale buffer cache information to be flushed.
	 */
	lwkt_gettoken(&nmp->nm_token);
	error = 0;
	if ((waitfor & MNT_LAZY) == 0) {
		while (error == 0 && scaninfo.rescan) {
			scaninfo.rescan = 0;
			error = vsyncscan(mp, VMSC_GETVP,
					  nfs_sync_scan2, &scaninfo);
		}
	}
	lwkt_reltoken(&nmp->nm_token);
	return(error);
}

static int
nfs_sync_scan2(struct mount *mp, struct vnode *vp, void *data)
{
    struct scaninfo *info = data;
    int error;

    if (vp->v_type == VNON || vp->v_type == VBAD)
	return(0);
    error = VOP_FSYNC(vp, info->waitfor, 0);
    if (error)
	info->allerror = error;
    return(0);
}

