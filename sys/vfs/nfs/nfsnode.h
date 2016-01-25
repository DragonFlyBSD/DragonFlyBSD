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
 *	@(#)nfsnode.h	8.9 (Berkeley) 5/14/95
 * $FreeBSD: /repoman/r/ncvs/src/sys/nfsclient/nfsnode.h,v 1.43 2004/04/14 23:23:55 peadar Exp $
 * $DragonFly: src/sys/vfs/nfs/nfsnode.h,v 1.18 2006/05/06 16:01:21 dillon Exp $
 */


#ifndef _NFS_NFSNODE_H_
#define _NFS_NFSNODE_H_

#if !defined(_NFS_NFS_H_) && !defined(_KERNEL)
#include "nfs.h"
#endif

#include <sys/lockf.h>

/*
 * Silly rename structure that hangs off the nfsnode until the name
 * can be removed by nfs_inactive()
 */
struct sillyrename {
	struct	ucred *s_cred;
	struct	vnode *s_dvp;
	long	s_namlen;
	char	s_name[20];
};

/*
 * Entries of directories in the buffer cache.
 */
struct nfs_dirent {
	ino_t		nfs_ino;	/* file number of entry */
	uint16_t	nfs_namlen;	/* strlen(d_name) */
	uint16_t	nfs_reclen;	/* total record length */
	uint8_t		nfs_type;	/* file type */
	uint8_t		nfs_padding1;
	uint8_t		nfs_padding2;
	uint8_t		nfs_padding3;
	char		nfs_name[];	/* name, NUL-terminated */
};

/*
 * This structure is used to save the logical directory offset to
 * NFS cookie mappings.
 * The mappings are stored in a list headed
 * by n_cookies, as required.
 * There is one mapping for each NFS_DIRBLKSIZ bytes of directory information
 * stored in increasing logical offset byte order.
 */
#define NFSNUMCOOKIES		31

struct nfsdmap {
	LIST_ENTRY(nfsdmap)	ndm_list;
	int			ndm_eocookie;
	nfsuint64		ndm_cookies[NFSNUMCOOKIES];
};

/*
 * The nfsnode is the nfs equivalent to ufs's inode. Any similarity
 * is purely coincidental.  There is a unique nfsnode allocated for
 * each active file, each current directory, each mounted-on file,
 * text file, and the root.
 *
 * An nfsnode is 'named' by its file handle. (nget/nfs_node.c)
 *
 * File handles are accessed via n_fhp, which will point to n_fh if the
 * file handle is small enough (<= NFS_SMALLFH).  Otherwise the file handle 
 * will be allocated.
 *
 * DragonFly does not pass ucreds to read and write operations, since such
 * operations are not possible unless the ucred has already been validated.
 * Validating ucreds are stored in nfsnode to pass on to NFS read/write RPCs.
 */
struct nfsnode {
	LIST_ENTRY(nfsnode)	n_hash;		/* Hash chain */
	u_quad_t		n_size;		/* Current size of file */
	u_quad_t		n_brev;		/* Modify rev when cached */
	struct vattr		n_vattr;	/* Vnode attribute cache */
	time_t			n_attrstamp;	/* Attr. cache timestamp */
	u_int32_t		n_mode;		/* ACCESS mode cache */
	uid_t			n_modeuid;	/* credentials having mode */
	time_t			n_modestamp;	/* mode cache timestamp */
	time_t			n_mtime;	/* Last known modified time */
	time_t			n_ctime;	/* Prev create time. */
	time_t			n_expiry;	/* Lease expiry time */
	nfsfh_t			*n_fhp;		/* NFS File Handle */
	struct ucred		*n_rucred;
	struct ucred		*n_wucred;
	struct vnode		*n_vnode;	/* associated vnode */
	struct lockf		n_lockf;	/* Locking record of file */
	int			n_error;	/* Save write error value */
	union {
		struct timespec	nf_atim;	/* Special file times */
		nfsuint64	nd_cookieverf;	/* Cookie verifier (dir only) */
	} n_un1;
	union {
		struct timespec	nf_mtim;
		off_t		nd_direof;	/* Dir. EOF offset cache */
	} n_un2;
	union {
		struct sillyrename *nf_silly;	/* Ptr to silly rename struct */
		LIST_HEAD(, nfsdmap) nd_cook;	/* cookies */
	} n_un3;
	short			n_fhsize;	/* size in bytes, of fh */
	short			n_flag;		/* Flag for locking.. */
	nfsfh_t			n_fh;		/* Small File Handle */
	struct lock		n_rslock;
};

#define n_atim		n_un1.nf_atim
#define n_mtim		n_un2.nf_mtim
#define n_sillyrename	n_un3.nf_silly
#define n_cookieverf	n_un1.nd_cookieverf
#define n_direofoffset	n_un2.nd_direof
#define n_cookies	n_un3.nd_cook

/*
 * Flags for n_flag
 */
#define	NFLUSHWANT	0x0001	/* Want wakeup from a flush in prog. */
#define	NFLUSHINPROG	0x0002	/* Avoid multiple calls to vinvalbuf() */
#define	NLMODIFIED	0x0004	/* Client has pending modifications */
#define	NWRITEERR	0x0008	/* Flag write errors so close will know */
#define	NUNUSED020	0x0020
#define	NUNUSED040	0x0040
#define	NQNFSEVICTED	0x0080	/* Has been evicted */
#define	NACC		0x0100	/* Special file accessed */
#define	NUPD		0x0200	/* Special file updated */
#define	NCHG		0x0400	/* Special file times changed */
#define	NLOCKED		0x0800  /* node is locked */
#define	NWANTED		0x0100  /* someone wants to lock */
#define	NRMODIFIED	0x2000  /* Server has unsynchronized modifications */

/*
 * Convert between nfsnode pointers and vnode pointers
 */
#define	VTONFS(vp)	((struct nfsnode *)(vp)->v_data)
#define	NFSTOV(np)	((struct vnode *)(np)->n_vnode)

/*
 * Queue head for nfsiod's
 */
extern TAILQ_HEAD(nfs_bufq, buf) nfs_bufq;

#if defined(_KERNEL)

/*
 *	nfs_rslock -	Attempt to obtain lock on nfsnode
 *
 *	Attempt to obtain a lock on the passed nfsnode, returning ENOLCK
 *	if the lock could not be obtained due to our having to sleep.  This
 *	function is generally used to lock around code that modifies an
 *	NFS file's size.  In order to avoid deadlocks the lock
 *	should not be obtained while other locks are being held.
 */

static __inline
int
nfs_rslock(struct nfsnode *np)
{
        return(lockmgr(&np->n_rslock, LK_EXCLUSIVE | LK_CANRECURSE |
		       LK_SLEEPFAIL));
}

static __inline
void
nfs_rsunlock(struct nfsnode *np)
{
	lockmgr(&np->n_rslock, LK_RELEASE);
}

static __inline
struct ucred *
nfs_vpcred(struct vnode *vp, int ndflag)
{
	struct nfsnode *np = VTONFS(vp);

	if (np && (ndflag & ND_WRITE) && np->n_wucred)
		return(np->n_wucred);
	if (np && (ndflag & ND_READ) && np->n_rucred)
		return(np->n_rucred);
	return(VFSTONFS((vp)->v_mount)->nm_cred);
}

/*
 * Prototypes for NFS vnode operations
 */
int	nfs_write (struct vop_write_args *);
int	nfs_inactive (struct vop_inactive_args *);
int	nfs_reclaim (struct vop_reclaim_args *);
int	nfs_flush (struct vnode *, int, struct thread *, int);

/* other stuff */
int	nfs_removeit (struct sillyrename *);
int	nfs_nget (struct mount *, nfsfh_t *, int,
		struct nfsnode **, struct vnode *);
int	nfs_nget_nonblock (struct mount *, nfsfh_t *, int,
		struct nfsnode **, struct vnode *);
nfsuint64 *nfs_getcookie (struct nfsnode *, off_t, int);
void	nfs_invaldir (struct vnode *);

#endif /* _KERNEL */

#endif
