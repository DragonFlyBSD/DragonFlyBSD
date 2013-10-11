/*
 * Copyright (c) 1989, 1993
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
 *	@(#)nfs_subs.c  8.8 (Berkeley) 5/22/95
 * $FreeBSD: /repoman/r/ncvs/src/sys/nfsclient/nfs_subs.c,v 1.128 2004/04/14 23:23:55 peadar Exp $
 */

/*
 * These functions support the macros and help fiddle mbuf chains for
 * the nfs op functions. They do things like create the rpc header and
 * copy data between mbuf chains and uio lists.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/nlookup.h>
#include <sys/namei.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/malloc.h>
#include <sys/sysent.h>
#include <sys/syscall.h>
#include <sys/conf.h>
#include <sys/objcache.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_extern.h>

#include <sys/buf2.h>

#include "rpcv2.h"
#include "nfsproto.h"
#include "nfs.h"
#include "nfsmount.h"
#include "nfsnode.h"
#include "xdr_subs.h"
#include "nfsm_subs.h"
#include "nfsrtt.h"

#include <netinet/in.h>

MALLOC_DEFINE(M_NFSMOUNT, "NFS mount", "NFS mount");

/*
 * Data items converted to xdr at startup, since they are constant
 * This is kinda hokey, but may save a little time doing byte swaps
 */
u_int32_t nfs_xdrneg1;
u_int32_t rpc_reply, rpc_msgdenied, rpc_mismatch, rpc_vers;
u_int32_t rpc_auth_unix, rpc_msgaccepted, rpc_call, rpc_autherr;
u_int32_t rpc_auth_kerb;
u_int32_t nfs_prog, nfs_true, nfs_false;

/* And other global data */
static enum vtype nv2tov_type[8]= {
	VNON, VREG, VDIR, VBLK, VCHR, VLNK, VNON,  VNON 
};
enum vtype nv3tov_type[8]= {
	VNON, VREG, VDIR, VBLK, VCHR, VLNK, VSOCK, VFIFO
};

int nfs_ticks;

/*
 * Protect master lists only.  Primary protection uses the per-mount
 * and per nfssvc_sock tokens.
 */
struct lwkt_token nfs_token = LWKT_TOKEN_INITIALIZER(unp_token);

static long nfs_pbuf_freecnt = -1;	/* start out unlimited */

struct nfsmount_head nfs_mountq = TAILQ_HEAD_INITIALIZER(nfs_mountq);
struct nfssvc_sockhead nfssvc_sockhead;
int nfssvc_sockhead_flag;
struct nfsd_head nfsd_head;
int nfsd_head_flag;
struct nfs_bufq nfs_bufq;
struct nqfhhashhead *nqfhhashtbl;
u_long nqfhhash;

static int nfs_prev_nfssvc_sy_narg;
static sy_call_t *nfs_prev_nfssvc_sy_call;

#ifndef NFS_NOSERVER

/*
 * Mapping of old NFS Version 2 RPC numbers to generic numbers.
 */
int nfsv3_procid[NFS_NPROCS] = {
	NFSPROC_NULL,
	NFSPROC_GETATTR,
	NFSPROC_SETATTR,
	NFSPROC_NOOP,
	NFSPROC_LOOKUP,
	NFSPROC_READLINK,
	NFSPROC_READ,
	NFSPROC_NOOP,
	NFSPROC_WRITE,
	NFSPROC_CREATE,
	NFSPROC_REMOVE,
	NFSPROC_RENAME,
	NFSPROC_LINK,
	NFSPROC_SYMLINK,
	NFSPROC_MKDIR,
	NFSPROC_RMDIR,
	NFSPROC_READDIR,
	NFSPROC_FSSTAT,
	NFSPROC_NOOP,
	NFSPROC_NOOP,
	NFSPROC_NOOP,
	NFSPROC_NOOP,
	NFSPROC_NOOP,
	NFSPROC_NOOP,
	NFSPROC_NOOP,
	NFSPROC_NOOP
};

#endif /* NFS_NOSERVER */
/*
 * and the reverse mapping from generic to Version 2 procedure numbers
 */
int nfsv2_procid[NFS_NPROCS] = {
	NFSV2PROC_NULL,
	NFSV2PROC_GETATTR,
	NFSV2PROC_SETATTR,
	NFSV2PROC_LOOKUP,
	NFSV2PROC_NOOP,
	NFSV2PROC_READLINK,
	NFSV2PROC_READ,
	NFSV2PROC_WRITE,
	NFSV2PROC_CREATE,
	NFSV2PROC_MKDIR,
	NFSV2PROC_SYMLINK,
	NFSV2PROC_CREATE,
	NFSV2PROC_REMOVE,
	NFSV2PROC_RMDIR,
	NFSV2PROC_RENAME,
	NFSV2PROC_LINK,
	NFSV2PROC_READDIR,
	NFSV2PROC_NOOP,
	NFSV2PROC_STATFS,
	NFSV2PROC_NOOP,
	NFSV2PROC_NOOP,
	NFSV2PROC_NOOP,
	NFSV2PROC_NOOP,
	NFSV2PROC_NOOP,
	NFSV2PROC_NOOP,
	NFSV2PROC_NOOP,
};

#ifndef NFS_NOSERVER
/*
 * Maps errno values to nfs error numbers.
 * Use NFSERR_IO as the catch all for ones not specifically defined in
 * RFC 1094.
 */
static u_char nfsrv_v2errmap[ELAST] = {
  NFSERR_PERM,	NFSERR_NOENT,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,
  NFSERR_NXIO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,
  NFSERR_IO,	NFSERR_IO,	NFSERR_ACCES,	NFSERR_IO,	NFSERR_IO,
  NFSERR_IO,	NFSERR_EXIST,	NFSERR_IO,	NFSERR_NODEV,	NFSERR_NOTDIR,
  NFSERR_ISDIR,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,
  NFSERR_IO,	NFSERR_FBIG,	NFSERR_NOSPC,	NFSERR_IO,	NFSERR_ROFS,
  NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,
  NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,
  NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,
  NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,
  NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,
  NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,
  NFSERR_IO,	NFSERR_IO,	NFSERR_NAMETOL,	NFSERR_IO,	NFSERR_IO,
  NFSERR_NOTEMPTY, NFSERR_IO,	NFSERR_IO,	NFSERR_DQUOT,	NFSERR_STALE,
  NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,
  NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,
  NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,
  NFSERR_IO /* << Last is 86 */
};

/*
 * Maps errno values to nfs error numbers.
 * Although it is not obvious whether or not NFS clients really care if
 * a returned error value is in the specified list for the procedure, the
 * safest thing to do is filter them appropriately. For Version 2, the
 * X/Open XNFS document is the only specification that defines error values
 * for each RPC (The RFC simply lists all possible error values for all RPCs),
 * so I have decided to not do this for Version 2.
 * The first entry is the default error return and the rest are the valid
 * errors for that RPC in increasing numeric order.
 */
static short nfsv3err_null[] = {
	0,
	0,
};

static short nfsv3err_getattr[] = {
	NFSERR_IO,
	NFSERR_IO,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_SERVERFAULT,
	0,
};

static short nfsv3err_setattr[] = {
	NFSERR_IO,
	NFSERR_PERM,
	NFSERR_IO,
	NFSERR_ACCES,
	NFSERR_INVAL,
	NFSERR_NOSPC,
	NFSERR_ROFS,
	NFSERR_DQUOT,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_NOT_SYNC,
	NFSERR_SERVERFAULT,
	0,
};

static short nfsv3err_lookup[] = {
	NFSERR_IO,
	NFSERR_NOENT,
	NFSERR_IO,
	NFSERR_ACCES,
	NFSERR_NOTDIR,
	NFSERR_NAMETOL,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_SERVERFAULT,
	0,
};

static short nfsv3err_access[] = {
	NFSERR_IO,
	NFSERR_IO,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_SERVERFAULT,
	0,
};

static short nfsv3err_readlink[] = {
	NFSERR_IO,
	NFSERR_IO,
	NFSERR_ACCES,
	NFSERR_INVAL,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_NOTSUPP,
	NFSERR_SERVERFAULT,
	0,
};

static short nfsv3err_read[] = {
	NFSERR_IO,
	NFSERR_IO,
	NFSERR_NXIO,
	NFSERR_ACCES,
	NFSERR_INVAL,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_SERVERFAULT,
	0,
};

static short nfsv3err_write[] = {
	NFSERR_IO,
	NFSERR_IO,
	NFSERR_ACCES,
	NFSERR_INVAL,
	NFSERR_FBIG,
	NFSERR_NOSPC,
	NFSERR_ROFS,
	NFSERR_DQUOT,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_SERVERFAULT,
	0,
};

static short nfsv3err_create[] = {
	NFSERR_IO,
	NFSERR_IO,
	NFSERR_ACCES,
	NFSERR_EXIST,
	NFSERR_NOTDIR,
	NFSERR_NOSPC,
	NFSERR_ROFS,
	NFSERR_NAMETOL,
	NFSERR_DQUOT,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_NOTSUPP,
	NFSERR_SERVERFAULT,
	0,
};

static short nfsv3err_mkdir[] = {
	NFSERR_IO,
	NFSERR_IO,
	NFSERR_ACCES,
	NFSERR_EXIST,
	NFSERR_NOTDIR,
	NFSERR_NOSPC,
	NFSERR_ROFS,
	NFSERR_NAMETOL,
	NFSERR_DQUOT,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_NOTSUPP,
	NFSERR_SERVERFAULT,
	0,
};

static short nfsv3err_symlink[] = {
	NFSERR_IO,
	NFSERR_IO,
	NFSERR_ACCES,
	NFSERR_EXIST,
	NFSERR_NOTDIR,
	NFSERR_NOSPC,
	NFSERR_ROFS,
	NFSERR_NAMETOL,
	NFSERR_DQUOT,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_NOTSUPP,
	NFSERR_SERVERFAULT,
	0,
};

static short nfsv3err_mknod[] = {
	NFSERR_IO,
	NFSERR_IO,
	NFSERR_ACCES,
	NFSERR_EXIST,
	NFSERR_NOTDIR,
	NFSERR_NOSPC,
	NFSERR_ROFS,
	NFSERR_NAMETOL,
	NFSERR_DQUOT,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_NOTSUPP,
	NFSERR_SERVERFAULT,
	NFSERR_BADTYPE,
	0,
};

static short nfsv3err_remove[] = {
	NFSERR_IO,
	NFSERR_NOENT,
	NFSERR_IO,
	NFSERR_ACCES,
	NFSERR_NOTDIR,
	NFSERR_ROFS,
	NFSERR_NAMETOL,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_SERVERFAULT,
	0,
};

static short nfsv3err_rmdir[] = {
	NFSERR_IO,
	NFSERR_NOENT,
	NFSERR_IO,
	NFSERR_ACCES,
	NFSERR_EXIST,
	NFSERR_NOTDIR,
	NFSERR_INVAL,
	NFSERR_ROFS,
	NFSERR_NAMETOL,
	NFSERR_NOTEMPTY,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_NOTSUPP,
	NFSERR_SERVERFAULT,
	0,
};

static short nfsv3err_rename[] = {
	NFSERR_IO,
	NFSERR_NOENT,
	NFSERR_IO,
	NFSERR_ACCES,
	NFSERR_EXIST,
	NFSERR_XDEV,
	NFSERR_NOTDIR,
	NFSERR_ISDIR,
	NFSERR_INVAL,
	NFSERR_NOSPC,
	NFSERR_ROFS,
	NFSERR_MLINK,
	NFSERR_NAMETOL,
	NFSERR_NOTEMPTY,
	NFSERR_DQUOT,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_NOTSUPP,
	NFSERR_SERVERFAULT,
	0,
};

static short nfsv3err_link[] = {
	NFSERR_IO,
	NFSERR_IO,
	NFSERR_ACCES,
	NFSERR_EXIST,
	NFSERR_XDEV,
	NFSERR_NOTDIR,
	NFSERR_INVAL,
	NFSERR_NOSPC,
	NFSERR_ROFS,
	NFSERR_MLINK,
	NFSERR_NAMETOL,
	NFSERR_DQUOT,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_NOTSUPP,
	NFSERR_SERVERFAULT,
	0,
};

static short nfsv3err_readdir[] = {
	NFSERR_IO,
	NFSERR_IO,
	NFSERR_ACCES,
	NFSERR_NOTDIR,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_BAD_COOKIE,
	NFSERR_TOOSMALL,
	NFSERR_SERVERFAULT,
	0,
};

static short nfsv3err_readdirplus[] = {
	NFSERR_IO,
	NFSERR_IO,
	NFSERR_ACCES,
	NFSERR_NOTDIR,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_BAD_COOKIE,
	NFSERR_NOTSUPP,
	NFSERR_TOOSMALL,
	NFSERR_SERVERFAULT,
	0,
};

static short nfsv3err_fsstat[] = {
	NFSERR_IO,
	NFSERR_IO,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_SERVERFAULT,
	0,
};

static short nfsv3err_fsinfo[] = {
	NFSERR_STALE,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_SERVERFAULT,
	0,
};

static short nfsv3err_pathconf[] = {
	NFSERR_STALE,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_SERVERFAULT,
	0,
};

static short nfsv3err_commit[] = {
	NFSERR_IO,
	NFSERR_IO,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_SERVERFAULT,
	0,
};

static short *nfsrv_v3errmap[] = {
	nfsv3err_null,
	nfsv3err_getattr,
	nfsv3err_setattr,
	nfsv3err_lookup,
	nfsv3err_access,
	nfsv3err_readlink,
	nfsv3err_read,
	nfsv3err_write,
	nfsv3err_create,
	nfsv3err_mkdir,
	nfsv3err_symlink,
	nfsv3err_mknod,
	nfsv3err_remove,
	nfsv3err_rmdir,
	nfsv3err_rename,
	nfsv3err_link,
	nfsv3err_readdir,
	nfsv3err_readdirplus,
	nfsv3err_fsstat,
	nfsv3err_fsinfo,
	nfsv3err_pathconf,
	nfsv3err_commit,
};

#endif /* NFS_NOSERVER */

struct nfssvc_args;
extern int sys_nfssvc(struct proc *, struct nfssvc_args *, int *);

/*
 * This needs to return a monotonically increasing or close to monotonically
 * increasing result, otherwise the write gathering queues won't work 
 * properly.
 */
u_quad_t
nfs_curusec(void) 
{
	struct timeval tv;
	
	getmicrouptime(&tv);
	return ((u_quad_t)tv.tv_sec * 1000000 + (u_quad_t)tv.tv_usec);
}

/*
 * Called once to initialize data structures...
 */
int
nfs_init(struct vfsconf *vfsp)
{
	callout_init(&nfs_timer_handle);
	nfsmount_objcache = objcache_create_simple(M_NFSMOUNT, sizeof(struct nfsmount));

	nfs_mount_type = vfsp->vfc_typenum;
	nfsrtt.pos = 0;
	rpc_vers = txdr_unsigned(RPC_VER2);
	rpc_call = txdr_unsigned(RPC_CALL);
	rpc_reply = txdr_unsigned(RPC_REPLY);
	rpc_msgdenied = txdr_unsigned(RPC_MSGDENIED);
	rpc_msgaccepted = txdr_unsigned(RPC_MSGACCEPTED);
	rpc_mismatch = txdr_unsigned(RPC_MISMATCH);
	rpc_autherr = txdr_unsigned(RPC_AUTHERR);
	rpc_auth_unix = txdr_unsigned(RPCAUTH_UNIX);
	rpc_auth_kerb = txdr_unsigned(RPCAUTH_KERB4);
	nfs_prog = txdr_unsigned(NFS_PROG);
	nfs_true = txdr_unsigned(TRUE);
	nfs_false = txdr_unsigned(FALSE);
	nfs_xdrneg1 = txdr_unsigned(-1);
	nfs_ticks = (hz * NFS_TICKINTVL + 500) / 1000;
	if (nfs_ticks < 1)
		nfs_ticks = 1;
	nfs_nhinit();			/* Init the nfsnode table */
#ifndef NFS_NOSERVER
	nfsrv_init(0);			/* Init server data structures */
	nfsrv_initcache();		/* Init the server request cache */
#endif

	/*
	 * Mainly for vkernel operation.  If memory is severely limited
	 */
	if (nfs_maxasyncbio > nmbclusters * MCLBYTES / NFS_MAXDATA / 3)
		nfs_maxasyncbio = nmbclusters * MCLBYTES / NFS_MAXDATA / 3;
	if (nfs_maxasyncbio < 4)
		nfs_maxasyncbio = 4;

	/*
	 * Initialize reply list and start timer
	 */
	nfs_timer_callout(0);

	nfs_prev_nfssvc_sy_narg = sysent[SYS_nfssvc].sy_narg;
	sysent[SYS_nfssvc].sy_narg = 2;
	nfs_prev_nfssvc_sy_call = sysent[SYS_nfssvc].sy_call;
	sysent[SYS_nfssvc].sy_call = (sy_call_t *)sys_nfssvc;

	nfs_pbuf_freecnt = nswbuf / 2 + 1;

	return (0);
}

int
nfs_uninit(struct vfsconf *vfsp)
{
	callout_stop(&nfs_timer_handle);
	nfs_mount_type = -1;
	sysent[SYS_nfssvc].sy_narg = nfs_prev_nfssvc_sy_narg;
	sysent[SYS_nfssvc].sy_call = nfs_prev_nfssvc_sy_call;
	return (0);
}

/*
 * Attribute cache routines.
 * nfs_loadattrcache() - loads or updates the cache contents from attributes
 *	that are on the mbuf list
 * nfs_getattrcache() - returns valid attributes if found in cache, returns
 *	error otherwise
 */

/*
 * Load the attribute cache (that lives in the nfsnode entry) with
 * the values on the mbuf list.  Load *vaper with the attributes.  vaper
 * may be NULL.
 *
 * As a side effect n_mtime, which we use to determine if the file was
 * modified by some other host, is set to the attribute timestamp and
 * NRMODIFIED is set if the two values differ.
 *
 * WARNING: the mtime loaded into vaper does not necessarily represent
 * n_mtime or n_attr.mtime due to NACC and NUPD.
 */
int
nfs_loadattrcache(struct vnode *vp, struct mbuf **mdp, caddr_t *dposp,
		  struct vattr *vaper, int lattr_flags)
{
	struct vattr *vap;
	struct nfs_fattr *fp;
	struct nfsnode *np;
	int32_t t1;
	caddr_t cp2;
	int error = 0;
	int rmajor, rminor;
	udev_t rdev;
	struct mbuf *md;
	enum vtype vtyp;
	u_short vmode;
	struct timespec mtime;
	int v3 = NFS_ISV3(vp);

	md = *mdp;
	t1 = (mtod(md, caddr_t) + md->m_len) - *dposp;
	if ((error = nfsm_disct(mdp, dposp, NFSX_FATTR(v3), t1, &cp2)) != 0)
		return (error);
	fp = (struct nfs_fattr *)cp2;
	if (v3) {
		vtyp = nfsv3tov_type(fp->fa_type);
		vmode = fxdr_unsigned(u_short, fp->fa_mode);
		rmajor = (int)fxdr_unsigned(int, fp->fa3_rdev.specdata1);
		rminor = (int)fxdr_unsigned(int, fp->fa3_rdev.specdata2);
		fxdr_nfsv3time(&fp->fa3_mtime, &mtime);
	} else {
		vtyp = nfsv2tov_type(fp->fa_type);
		vmode = fxdr_unsigned(u_short, fp->fa_mode);
		/*
		 * XXX
		 *
		 * The duplicate information returned in fa_type and fa_mode
		 * is an ambiguity in the NFS version 2 protocol.
		 *
		 * VREG should be taken literally as a regular file.  If a
		 * server intents to return some type information differently
		 * in the upper bits of the mode field (e.g. for sockets, or
		 * FIFOs), NFSv2 mandates fa_type to be VNON.  Anyway, we
		 * leave the examination of the mode bits even in the VREG
		 * case to avoid breakage for bogus servers, but we make sure
		 * that there are actually type bits set in the upper part of
		 * fa_mode (and failing that, trust the va_type field).
		 *
		 * NFSv3 cleared the issue, and requires fa_mode to not
		 * contain any type information (while also introduing sockets
		 * and FIFOs for fa_type).
		 */
		if (vtyp == VNON || (vtyp == VREG && (vmode & S_IFMT) != 0))
			vtyp = IFTOVT(vmode);
		rdev = fxdr_unsigned(int32_t, fp->fa2_rdev);
		rmajor = umajor(rdev);
		rminor = uminor(rdev);
		fxdr_nfsv2time(&fp->fa2_mtime, &mtime);

		/*
		 * Really ugly NFSv2 kludge.
		 */
		if (vtyp == VCHR && rdev == (udev_t)0xffffffff)
			vtyp = VFIFO;
	}

	/*
	 * If v_type == VNON it is a new node, so fill in the v_type,
	 * n_mtime fields. Check to see if it represents a special
	 * device, and if so, check for a possible alias. Once the
	 * correct vnode has been obtained, fill in the rest of the
	 * information.
	 */
	np = VTONFS(vp);
	if (vp->v_type != vtyp) {
		nfs_setvtype(vp, vtyp);
		if (vp->v_type == VFIFO) {
			vp->v_ops = &vp->v_mount->mnt_vn_fifo_ops;
		} else if (vp->v_type == VCHR || vp->v_type == VBLK) {
			vp->v_ops = &vp->v_mount->mnt_vn_spec_ops;
			addaliasu(vp, rmajor, rminor);
		} else {
			vp->v_ops = &vp->v_mount->mnt_vn_use_ops;
		}
		np->n_mtime = mtime.tv_sec;
	} else if (np->n_mtime != mtime.tv_sec) {
		/*
		 * If we haven't modified the file locally and the server
		 * timestamp does not match, then the server probably
		 * modified the file.  We must flag this condition so
		 * the proper syncnronization can be done.  We do not
		 * try to synchronize the state here because that
		 * could lead to an endless recursion.
		 *
		 * XXX loadattrcache can be set during the reply to a write,
		 * before the write timestamp is properly processed.  To
		 * avoid unconditionally setting the rmodified bit (which
		 * has the effect of flushing the cache), we only do this
		 * check if the lmodified bit is not set.
		 */
		np->n_mtime = mtime.tv_sec;
		if ((lattr_flags & NFS_LATTR_NOMTIMECHECK) == 0)
			np->n_flag |= NRMODIFIED;
	}
	vap = &np->n_vattr;
	vap->va_type = vtyp;
	vap->va_mode = (vmode & 07777);
	vap->va_rmajor = rmajor;
	vap->va_rminor = rminor;
	vap->va_mtime = mtime;
	vap->va_fsid = vp->v_mount->mnt_stat.f_fsid.val[0];
	if (v3) {
		vap->va_nlink = fxdr_unsigned(u_short, fp->fa_nlink);
		vap->va_uid = fxdr_unsigned(uid_t, fp->fa_uid);
		vap->va_gid = fxdr_unsigned(gid_t, fp->fa_gid);
		vap->va_size = fxdr_hyper(&fp->fa3_size);
		vap->va_blocksize = NFS_FABLKSIZE;
		vap->va_bytes = fxdr_hyper(&fp->fa3_used);
		vap->va_fileid = fxdr_hyper(&fp->fa3_fileid);
		fxdr_nfsv3time(&fp->fa3_atime, &vap->va_atime);
		fxdr_nfsv3time(&fp->fa3_ctime, &vap->va_ctime);
		vap->va_flags = 0;
		vap->va_filerev = 0;
	} else {
		vap->va_nlink = fxdr_unsigned(u_short, fp->fa_nlink);
		vap->va_uid = fxdr_unsigned(uid_t, fp->fa_uid);
		vap->va_gid = fxdr_unsigned(gid_t, fp->fa_gid);
		vap->va_size = fxdr_unsigned(u_int32_t, fp->fa2_size);
		vap->va_blocksize = fxdr_unsigned(int32_t, fp->fa2_blocksize);
		vap->va_bytes = (u_quad_t)fxdr_unsigned(int32_t, fp->fa2_blocks)
		    * NFS_FABLKSIZE;
		vap->va_fileid = fxdr_unsigned(int32_t, fp->fa2_fileid);
		fxdr_nfsv2time(&fp->fa2_atime, &vap->va_atime);
		vap->va_flags = 0;
		vap->va_ctime.tv_sec = fxdr_unsigned(u_int32_t,
		    fp->fa2_ctime.nfsv2_sec);
		vap->va_ctime.tv_nsec = 0;
		vap->va_gen = fxdr_unsigned(u_int32_t,fp->fa2_ctime.nfsv2_usec);
		vap->va_filerev = 0;
	}
	np->n_attrstamp = time_uptime;
	if (vap->va_size != np->n_size) {
		if (vap->va_type == VREG) {
			/*
			 * Get rid of all the junk we had before and just
			 * set NRMODIFIED if NLMODIFIED is 0.  Depend on
			 * occassionally flushing our dirty buffers to
			 * clear both the NLMODIFIED and NRMODIFIED flags.
			 */
			if ((np->n_flag & NLMODIFIED) == 0)
				np->n_flag |= NRMODIFIED;
#if 0
			if ((lattr_flags & NFS_LATTR_NOSHRINK) && 
			    vap->va_size < np->n_size) {
				/*
				 * We've been told not to shrink the file;
				 * zero np->n_attrstamp to indicate that
				 * the attributes are stale.
				 *
				 * This occurs primarily due to recursive
				 * NFS ops that are executed during periods
				 * where we cannot safely reduce the size of
				 * the file.
				 *
				 * Additionally, write rpcs are broken down
				 * into buffers and np->n_size is 
				 * pre-extended.  Setting NRMODIFIED here
				 * can result in n_size getting reset to a
				 * lower value, which is NOT what we want.
				 * XXX this needs to be cleaned up a lot 
				 * more.
				 */
				vap->va_size = np->n_size;
				np->n_attrstamp = 0;
				if ((np->n_flag & NLMODIFIED) == 0)
					np->n_flag |= NRMODIFIED;
			} else if (np->n_flag & NLMODIFIED) {
				/*
				 * We've modified the file: Use the larger
				 * of our size, and the server's size.  At
				 * this point the cache coherency is all
				 * shot to hell.  To try to handle multiple
				 * clients appending to the file at the same
				 * time mark that the server has changed
				 * the file if the server's notion of the
				 * file size is larger then our notion.
				 *
				 * XXX this needs work.
				 */
				if (vap->va_size < np->n_size) {
					vap->va_size = np->n_size;
				} else {
					np->n_size = vap->va_size;
					np->n_flag |= NRMODIFIED;
				}
			} else {
				/*
				 * Someone changed the file's size on the
				 * server and there are no local changes
				 * to get in the way, set the size and mark
				 * it.
				 */
				np->n_size = vap->va_size;
				np->n_flag |= NRMODIFIED;
			}
			nvnode_pager_setsize(vp, np->n_size, XXX);
#endif
		} else {
			np->n_size = vap->va_size;
		}
	}
	if (vaper != NULL) {
		bcopy((caddr_t)vap, (caddr_t)vaper, sizeof(*vap));
		if (np->n_flag & NCHG) {
			if (np->n_flag & NACC)
				vaper->va_atime = np->n_atim;
			if (np->n_flag & NUPD)
				vaper->va_mtime = np->n_mtim;
		}
	}
	return (0);
}

#ifdef NFS_ACDEBUG
#include <sys/sysctl.h>
SYSCTL_DECL(_vfs_nfs);
static int nfs_acdebug;
SYSCTL_INT(_vfs_nfs, OID_AUTO, acdebug, CTLFLAG_RW, &nfs_acdebug, 0, "");
#endif

/*
 * Check the time stamp
 * If the cache is valid, copy contents to *vap and return 0
 * otherwise return an error
 */
int
nfs_getattrcache(struct vnode *vp, struct vattr *vaper)
{
	struct nfsnode *np;
	struct vattr *vap;
	struct nfsmount *nmp;
	int timeo;

	np = VTONFS(vp);
	vap = &np->n_vattr;
	nmp = VFSTONFS(vp->v_mount);

	/*
	 * Dynamic timeout based on how recently the file was modified.
	 * n_mtime is always valid.
	 */
	timeo = (get_approximate_time_t() - np->n_mtime) / 60;

#ifdef NFS_ACDEBUG
	if (nfs_acdebug>1)
		kprintf("nfs_getattrcache: initial timeo = %d\n", timeo);
#endif

	if (vap->va_type == VDIR) {
		if ((np->n_flag & NLMODIFIED) || timeo < nmp->nm_acdirmin)
			timeo = nmp->nm_acdirmin;
		else if (timeo > nmp->nm_acdirmax)
			timeo = nmp->nm_acdirmax;
	} else {
		if ((np->n_flag & NLMODIFIED) || timeo < nmp->nm_acregmin)
			timeo = nmp->nm_acregmin;
		else if (timeo > nmp->nm_acregmax)
			timeo = nmp->nm_acregmax;
	}

#ifdef NFS_ACDEBUG
	if (nfs_acdebug > 2)
		kprintf("acregmin %d; acregmax %d; acdirmin %d; acdirmax %d\n",
			nmp->nm_acregmin, nmp->nm_acregmax,
			nmp->nm_acdirmin, nmp->nm_acdirmax);

	if (nfs_acdebug)
		kprintf("nfs_getattrcache: age = %d; final timeo = %d\n",
			(int)(time_uptime - np->n_attrstamp), timeo);
#endif

	if (np->n_attrstamp == 0 || (time_uptime - np->n_attrstamp) >= timeo) {
		nfsstats.attrcache_misses++;
		return (ENOENT);
	}
	nfsstats.attrcache_hits++;

	/*
	 * Our attribute cache can be stale due to modifications made on
	 * this host.  XXX this is a bad hack.  We need a more deterministic
	 * means of finding out which np fields are valid verses attr cache
	 * fields.  We really should update the vattr info on the fly when
	 * making local changes.
	 */
	if (vap->va_size != np->n_size) {
		if (vap->va_type == VREG) {
			if (np->n_flag & NLMODIFIED)
				vap->va_size = np->n_size;
			nfs_meta_setsize(vp, curthread, vap->va_size, 0);
		} else {
			np->n_size = vap->va_size;
		}
	}
	bcopy((caddr_t)vap, (caddr_t)vaper, sizeof(struct vattr));
	if (np->n_flag & NCHG) {
		if (np->n_flag & NACC)
			vaper->va_atime = np->n_atim;
		if (np->n_flag & NUPD)
			vaper->va_mtime = np->n_mtim;
	}
	return (0);
}

#ifndef NFS_NOSERVER

/*
 * Set up nameidata for a lookup() call and do it.
 *
 * If pubflag is set, this call is done for a lookup operation on the
 * public filehandle. In that case we allow crossing mountpoints and
 * absolute pathnames. However, the caller is expected to check that
 * the lookup result is within the public fs, and deny access if
 * it is not.
 *
 * dirp may be set whether an error is returned or not, and must be 
 * released by the caller.
 *
 * On return nd->nl_nch usually points to the target ncp, which may represent
 * a negative hit.
 *
 * NOTE: the caller must call nlookup_done(nd) unconditionally on return
 * to cleanup.
 */
int
nfs_namei(struct nlookupdata *nd, struct ucred *cred, int nflags,
	struct vnode **dvpp, struct vnode **vpp,
	fhandle_t *fhp, int len,
	struct nfssvc_sock *slp, struct sockaddr *nam, struct mbuf **mdp,
	caddr_t *dposp, struct vnode **dirpp, struct thread *td,
	int kerbflag, int pubflag)
{
	int i, rem;
	struct mbuf *md;
	char *fromcp, *tocp, *cp;
	char *namebuf;
	struct nchandle nch;
	struct vnode *dp;
	struct mount *mp;
	int error, rdonly;

	namebuf = objcache_get(namei_oc, M_WAITOK);
	*dirpp = NULL;

	/*
	 * Copy the name from the mbuf list to namebuf.
	 */
	fromcp = *dposp;
	tocp = namebuf;
	md = *mdp;
	rem = mtod(md, caddr_t) + md->m_len - fromcp;
	for (i = 0; i < len; i++) {
		while (rem == 0) {
			md = md->m_next;
			if (md == NULL) {
				error = EBADRPC;
				goto out;
			}
			fromcp = mtod(md, caddr_t);
			rem = md->m_len;
		}
		if (*fromcp == '\0' || (!pubflag && *fromcp == '/')) {
			error = EACCES;
			goto out;
		}
		*tocp++ = *fromcp++;
		rem--;
	}
	*tocp = '\0';
	*mdp = md;
	*dposp = fromcp;
	len = nfsm_rndup(len)-len;
	if (len > 0) {
		if (rem >= len)
			*dposp += len;
		else if ((error = nfs_adv(mdp, dposp, len, rem)) != 0)
			goto out;
	}

	/*
	 * Extract and set starting directory.  The returned dp is refd
	 * but not locked.
	 */
	error = nfsrv_fhtovp(fhp, FALSE, &mp, &dp, cred, slp,
				nam, &rdonly, kerbflag, pubflag);
	if (error)
		goto out;
	if (dp->v_type != VDIR) {
		vrele(dp);
		error = ENOTDIR;
		goto out;
	}

	/*
	 * Set return directory.  Reference to dp is implicitly transfered 
	 * to the returned pointer.  This must be set before we potentially
	 * goto out below.
	 */
	*dirpp = dp;

	/*
	 * read-only - NLC_DELETE, NLC_RENAME_DST are disallowed.  NLC_CREATE
	 *	       is passed through to nlookup() and will be disallowed
	 *	       if the file does not already exist.
	 */
	if (rdonly) {
		nflags |= NLC_NFS_RDONLY;
		if (nflags & (NLC_DELETE | NLC_RENAME_DST)) {
			error = EROFS;
			goto out;
		}
	}

	/*
	 * Oh joy. For WebNFS, handle those pesky '%' escapes,
	 * and the 'native path' indicator.
	 */
	if (pubflag) {
		cp = objcache_get(namei_oc, M_WAITOK);
		fromcp = namebuf;
		tocp = cp;
		if ((unsigned char)*fromcp >= WEBNFS_SPECCHAR_START) {
			switch ((unsigned char)*fromcp) {
			case WEBNFS_NATIVE_CHAR:
				/*
				 * 'Native' path for us is the same
				 * as a path according to the NFS spec,
				 * just skip the escape char.
				 */
				fromcp++;
				break;
			/*
			 * More may be added in the future, range 0x80-0xff
			 */
			default:
				error = EIO;
				objcache_put(namei_oc, cp);
				goto out;
			}
		}
		/*
		 * Translate the '%' escapes, URL-style.
		 */
		while (*fromcp != '\0') {
			if (*fromcp == WEBNFS_ESC_CHAR) {
				if (fromcp[1] != '\0' && fromcp[2] != '\0') {
					fromcp++;
					*tocp++ = HEXSTRTOI(fromcp);
					fromcp += 2;
					continue;
				} else {
					error = ENOENT;
					objcache_put(namei_oc, cp);
					goto out;
				}
			} else
				*tocp++ = *fromcp++;
		}
		*tocp = '\0';
		objcache_put(namei_oc, namebuf);
		namebuf = cp;
	}

	/*
	 * Setup for search.  We need to get a start directory from dp.  Note
	 * that dp is ref'd, but we no longer 'own' the ref (*dirpp owns it).
	 */
	if (pubflag == 0) {
		nflags |= NLC_NFS_NOSOFTLINKTRAV;
		nflags |= NLC_NOCROSSMOUNT;
	}

	/*
	 * We need a starting ncp from the directory vnode dp.  dp must not
	 * be locked.  The returned ncp will be refd but not locked. 
	 *
	 * If no suitable ncp is found we instruct cache_fromdvp() to create
	 * one.  If this fails the directory has probably been removed while
	 * the target was chdir'd into it and any further lookup will fail.
	 */
	if ((error = cache_fromdvp(dp, cred, 1, &nch)) != 0)
		goto out;
	nlookup_init_raw(nd, namebuf, UIO_SYSSPACE, nflags, cred, &nch);
	cache_drop(&nch);

	/*
	 * Ok, do the lookup.
	 */
	error = nlookup(nd);

	/*
	 * If no error occured return the requested dvpp and vpp.  If
	 * NLC_CREATE was specified nd->nl_nch may represent a negative
	 * cache hit in which case we do not attempt to obtain the vp.
	 */
	if (error == 0) {
		if (dvpp) {
			if (nd->nl_nch.ncp->nc_parent) {
				nch = nd->nl_nch;
				nch.ncp = nch.ncp->nc_parent;
				cache_hold(&nch);
				cache_lock(&nch);
				error = cache_vget(&nch, nd->nl_cred,
						   LK_EXCLUSIVE, dvpp);
				cache_put(&nch);
			} else {
				error = ENXIO;
			}
		}
		if (vpp && nd->nl_nch.ncp->nc_vp) {
			error = cache_vget(&nd->nl_nch, nd->nl_cred, LK_EXCLUSIVE, vpp);
		}
		if (error) {
			if (dvpp && *dvpp) {
				vput(*dvpp);
				*dvpp = NULL;
			}
			if (vpp && *vpp) {
				vput(*vpp);
				*vpp = NULL;
			}
		}
	}

	/*
	 * Finish up.
	 */
out:
	objcache_put(namei_oc, namebuf);
	return (error);
}

/*
 * nfsrv_fhtovp() - convert a fh to a vnode ptr (optionally locked)
 * 	- look up fsid in mount list (if not found ret error)
 *	- get vp and export rights by calling VFS_FHTOVP()
 *	- if cred->cr_uid == 0 or MNT_EXPORTANON set it to credanon
 *	- if not lockflag unlock it with vn_unlock()
 */
int
nfsrv_fhtovp(fhandle_t *fhp, int lockflag,
	     struct mount **mpp, struct vnode **vpp,
	     struct ucred *cred, struct nfssvc_sock *slp, struct sockaddr *nam,
	     int *rdonlyp, int kerbflag, int pubflag)
{
	struct mount *mp;
	int i;
	struct ucred *credanon;
	int error, exflags;
#ifdef MNT_EXNORESPORT		/* XXX needs mountd and /etc/exports help yet */
	struct sockaddr_int *saddr;
#endif

	*vpp = NULL;
	*mpp = NULL;

	if (nfs_ispublicfh(fhp)) {
		if (!pubflag || !nfs_pub.np_valid)
			return (ESTALE);
		fhp = &nfs_pub.np_handle;
	}

	mp = *mpp = vfs_getvfs(&fhp->fh_fsid);
	if (mp == NULL)
		return (ESTALE);
	error = VFS_CHECKEXP(mp, nam, &exflags, &credanon);
	if (error)
		return (error); 
	error = VFS_FHTOVP(mp, NULL, &fhp->fh_fid, vpp);
	if (error)
		return (ESTALE);
#ifdef MNT_EXNORESPORT
	if (!(exflags & (MNT_EXNORESPORT|MNT_EXPUBLIC))) {
		saddr = (struct sockaddr_in *)nam;
		if (saddr->sin_family == AF_INET &&
		    ntohs(saddr->sin_port) >= IPPORT_RESERVED) {
			vput(*vpp);
			*vpp = NULL;
			return (NFSERR_AUTHERR | AUTH_TOOWEAK);
		}
	}
#endif
	/*
	 * Check/setup credentials.
	 */
	if (exflags & MNT_EXKERB) {
		if (!kerbflag) {
			vput(*vpp);
			*vpp = NULL;
			return (NFSERR_AUTHERR | AUTH_TOOWEAK);
		}
	} else if (kerbflag) {
		vput(*vpp);
		*vpp = NULL;
		return (NFSERR_AUTHERR | AUTH_TOOWEAK);
	} else if (cred->cr_uid == 0 || (exflags & MNT_EXPORTANON)) {
		cred->cr_uid = credanon->cr_uid;
		for (i = 0; i < credanon->cr_ngroups && i < NGROUPS; i++)
			cred->cr_groups[i] = credanon->cr_groups[i];
		cred->cr_ngroups = i;
	}
	if (exflags & MNT_EXRDONLY)
		*rdonlyp = 1;
	else
		*rdonlyp = 0;

	if (!lockflag)
		vn_unlock(*vpp);
	return (0);
}

/*
 * WebNFS: check if a filehandle is a public filehandle. For v3, this
 * means a length of 0, for v2 it means all zeroes. nfsm_srvmtofh has
 * transformed this to all zeroes in both cases, so check for it.
 */
int
nfs_ispublicfh(fhandle_t *fhp)
{
	char *cp = (char *)fhp;
	int i;

	for (i = 0; i < NFSX_V3FH; i++)
		if (*cp++ != 0)
			return (FALSE);
	return (TRUE);
}
  
#endif /* NFS_NOSERVER */
/*
 * This function compares two net addresses by family and returns TRUE
 * if they are the same host.
 * If there is any doubt, return FALSE.
 * The AF_INET family is handled as a special case so that address mbufs
 * don't need to be saved to store "struct in_addr", which is only 4 bytes.
 */
int
netaddr_match(int family, union nethostaddr *haddr, struct sockaddr *nam)
{
	struct sockaddr_in *inetaddr;

	switch (family) {
	case AF_INET:
		inetaddr = (struct sockaddr_in *)nam;
		if (inetaddr->sin_family == AF_INET &&
		    inetaddr->sin_addr.s_addr == haddr->had_inetaddr)
			return (1);
		break;
	default:
		break;
	};
	return (0);
}

static nfsuint64 nfs_nullcookie = { { 0, 0 } };
/*
 * This function finds the directory cookie that corresponds to the
 * logical byte offset given.
 */
nfsuint64 *
nfs_getcookie(struct nfsnode *np, off_t off, int add)
{
	struct nfsdmap *dp, *dp2;
	int pos;

	pos = (uoff_t)off / NFS_DIRBLKSIZ;
	if (pos == 0 || off < 0) {
#ifdef DIAGNOSTIC
		if (add)
			panic("nfs getcookie add at <= 0");
#endif
		return (&nfs_nullcookie);
	}
	pos--;
	dp = np->n_cookies.lh_first;
	if (!dp) {
		if (add) {
			dp = kmalloc(sizeof(struct nfsdmap), M_NFSDIROFF,
				     M_WAITOK);
			dp->ndm_eocookie = 0;
			LIST_INSERT_HEAD(&np->n_cookies, dp, ndm_list);
		} else
			return (NULL);
	}
	while (pos >= NFSNUMCOOKIES) {
		pos -= NFSNUMCOOKIES;
		if (dp->ndm_list.le_next) {
			if (!add && dp->ndm_eocookie < NFSNUMCOOKIES &&
				pos >= dp->ndm_eocookie)
				return (NULL);
			dp = dp->ndm_list.le_next;
		} else if (add) {
			dp2 = kmalloc(sizeof(struct nfsdmap), M_NFSDIROFF,
				      M_WAITOK);
			dp2->ndm_eocookie = 0;
			LIST_INSERT_AFTER(dp, dp2, ndm_list);
			dp = dp2;
		} else
			return (NULL);
	}
	if (pos >= dp->ndm_eocookie) {
		if (add)
			dp->ndm_eocookie = pos + 1;
		else
			return (NULL);
	}
	return (&dp->ndm_cookies[pos]);
}

/*
 * Invalidate cached directory information, except for the actual directory
 * blocks (which are invalidated separately).
 * Done mainly to avoid the use of stale offset cookies.
 */
void
nfs_invaldir(struct vnode *vp)
{
	struct nfsnode *np = VTONFS(vp);

#ifdef DIAGNOSTIC
	if (vp->v_type != VDIR)
		panic("nfs: invaldir not dir");
#endif
	np->n_direofoffset = 0;
	np->n_cookieverf.nfsuquad[0] = 0;
	np->n_cookieverf.nfsuquad[1] = 0;
	if (np->n_cookies.lh_first)
		np->n_cookies.lh_first->ndm_eocookie = 0;
}

/*
 * Set the v_type field for an NFS client's vnode and initialize for
 * buffer cache operations if necessary.
 */
void
nfs_setvtype(struct vnode *vp, enum vtype vtyp)
{
	vp->v_type = vtyp;

	switch(vtyp) {
	case VREG:
	case VDIR:
	case VLNK:
		/*
		 * Needs VMIO, size not yet known, and blocksize
		 * is not really relevant if we are passing a
		 * filesize of 0.
		 */
		vinitvmio(vp, 0, PAGE_SIZE, -1);
		break;
	default:
		break;
	}
}

/*
 * The write verifier has changed (probably due to a server reboot), so all
 * B_NEEDCOMMIT blocks will have to be written again. Since they are on the
 * dirty block list as B_DELWRI, all this takes is clearing the B_NEEDCOMMIT
 * and B_CLUSTEROK flags.  Once done the new write verifier can be set for the
 * mount point.
 *
 * B_CLUSTEROK must be cleared along with B_NEEDCOMMIT because stage 1 data 
 * writes are not clusterable.
 */

static int nfs_clearcommit_bp(struct buf *bp, void *data __unused);
static int nfs_clearcommit_callback(struct mount *mp, struct vnode *vp,
				    void *data __unused);

void
nfs_clearcommit(struct mount *mp)
{
	vsyncscan(mp, VMSC_NOWAIT, nfs_clearcommit_callback, NULL);
}

static int
nfs_clearcommit_callback(struct mount *mp, struct vnode *vp,
			 void *data __unused)
{
	lwkt_gettoken(&vp->v_token);
	RB_SCAN(buf_rb_tree, &vp->v_rbdirty_tree, NULL,
		nfs_clearcommit_bp, NULL);
	lwkt_reltoken(&vp->v_token);

	return(0);
}

static int
nfs_clearcommit_bp(struct buf *bp, void *data __unused)
{
	if (BUF_REFCNT(bp) == 0 &&
	    (bp->b_flags & (B_DELWRI | B_NEEDCOMMIT))
	     == (B_DELWRI | B_NEEDCOMMIT)) {
		bp->b_flags &= ~(B_NEEDCOMMIT | B_CLUSTEROK);
	}
	return(0);
}

#ifndef NFS_NOSERVER
/*
 * Map errnos to NFS error numbers. For Version 3 also filter out error
 * numbers not specified for the associated procedure.
 */
int
nfsrv_errmap(struct nfsrv_descript *nd, int err)
{
	short *defaulterrp, *errp;

	if (nd->nd_flag & ND_NFSV3) {
	    if (nd->nd_procnum <= NFSPROC_COMMIT) {
		errp = defaulterrp = nfsrv_v3errmap[nd->nd_procnum];
		while (*++errp) {
			if (*errp == err)
				return (err);
			else if (*errp > err)
				break;
		}
		return ((int)*defaulterrp);
	    } else
		return (err & 0xffff);
	}
	if (err <= ELAST)
		return ((int)nfsrv_v2errmap[err - 1]);
	return (NFSERR_IO);
}

/*
 * Sort the group list in increasing numerical order.
 * (Insertion sort by Chris Torek, who was grossed out by the bubble sort
 *  that used to be here.)
 */
void
nfsrvw_sort(gid_t *list, int num)
{
	int i, j;
	gid_t v;

	/* Insertion sort. */
	for (i = 1; i < num; i++) {
		v = list[i];
		/* find correct slot for value v, moving others up */
		for (j = i; --j >= 0 && v < list[j];)
			list[j + 1] = list[j];
		list[j + 1] = v;
	}
}

/*
 * copy credentials making sure that the result can be compared with bcmp().
 */
void
nfsrv_setcred(struct ucred *incred, struct ucred *outcred)
{
	int i;

	bzero((caddr_t)outcred, sizeof (struct ucred));
	outcred->cr_ref = 1;
	outcred->cr_uid = incred->cr_uid;
	outcred->cr_ngroups = incred->cr_ngroups;
	for (i = 0; i < incred->cr_ngroups; i++)
		outcred->cr_groups[i] = incred->cr_groups[i];
	nfsrvw_sort(outcred->cr_groups, outcred->cr_ngroups);
}
#endif /* NFS_NOSERVER */
