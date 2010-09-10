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
 *	@(#)nfsmount.h	8.3 (Berkeley) 3/30/95
 * $FreeBSD: src/sys/nfs/nfsmount.h,v 1.17 1999/12/29 04:54:54 peter Exp $
 * $DragonFly: src/sys/vfs/nfs/nfsmount.h,v 1.8 2006/04/07 06:38:33 dillon Exp $
 */


#ifndef _NFS_NFSMOUNT_H_
#define _NFS_NFSMOUNT_H_

#include <sys/mutex.h>
#include <sys/thread.h>	/* token */

enum nfssvc_state {
	NFSSVC_INIT,
	NFSSVC_WAITING,
	NFSSVC_PENDING,
	NFSSVC_STOPPING,
	NFSSVC_DONE
};

/*
 * Mount structure.
 * One allocated on every NFS mount.
 * Holds NFS specific information for mount.
 */
struct	nfsmount {
	int	nm_flag;		/* Flags for soft/hard... */
	int	nm_state;		/* Internal state flags */
	TAILQ_ENTRY(nfsmount) nm_entry;	/* entry in nfsmountq */
	struct mtx    nm_rxlock;	/* receive socket lock */
	struct mtx    nm_txlock;	/* send socket lock */
	thread_t nm_rxthread;
	thread_t nm_txthread;
	enum nfssvc_state nm_rxstate;
	enum nfssvc_state nm_txstate;
	struct	mount *nm_mountp;	/* Vfs structure for this filesystem */
	int	nm_numgrps;		/* Max. size of groupslist */
	u_char	nm_fh[NFSX_V3FHMAX];	/* File handle of root dir */
	int	nm_fhsize;		/* Size of root file handle */
	struct	socket *nm_so;		/* Rpc socket */
	int	nm_sotype;		/* Type of socket */
	int	nm_soproto;		/* and protocol */
	int	nm_soflags;		/* pr_flags for socket protocol */
	struct	sockaddr *nm_nam;	/* Addr of server */
	int	nm_timeo;		/* Init timer for NFSMNT_DUMBTIMR */
	int	nm_retry;		/* Max retries */
	int	nm_srtt[6];		/* Timers for rpcs (see proct[]) */
	int	nm_sdrtt[6];
	int	nm_maxasync_scaled;	/* Used to control congestion */
	int	nm_timeouts;		/* Request timeouts */
	int	nm_deadthresh;		/* Threshold of timeouts-->dead server*/
	u_int32_t nm_lastreprocnum;	/* Last resent procnum for dup detect */
	int	nm_rsize;		/* Max size of read rpc */
	int	nm_wsize;		/* Max size of write rpc */
	int	nm_readdirsize;		/* Size of a readdir rpc */
	int	nm_readahead;		/* Num. of blocks to readahead */
	int	nm_unused01;		/* Term (sec) for NQNFS lease */
	int	nm_acdirmin;		/* Directory attr cache min lifetime */
	int	nm_acdirmax;		/* Directory attr cache max lifetime */
	int	nm_acregmin;		/* Reg file attr cache min lifetime */
	int	nm_acregmax;		/* Reg file attr cache max lifetime */
	uid_t	nm_authuid;		/* Uid for authenticator */
	int	nm_authtype;		/* Authenticator type */
	int	nm_authlen;		/* and length */
	char	*nm_authstr;		/* Authenticator string */
	char	*nm_verfstr;		/* and the verifier */
	int	nm_verflen;
	u_char	nm_verf[NFSX_V3WRITEVERF]; /* V3 write verifier */
	NFSKERBKEY_T nm_key;		/* and the session key */
	int	nm_numuids;		/* Number of nfsuid mappings */
	TAILQ_HEAD(, nfsuid) nm_uidlruhead; /* Lists of nfsuid mappings */
	LIST_HEAD(, nfsuid) nm_uidhashtbl[NFS_MUIDHASHSIZ];
	TAILQ_HEAD(, bio) nm_bioq;	/* async io buffer queue */
	TAILQ_HEAD(, nfsreq) nm_reqtxq;	/* nfsreq queue - tx processing */
	TAILQ_HEAD(, nfsreq) nm_reqrxq;	/* nfsreq queue - rx processing */
	TAILQ_HEAD(, nfsreq) nm_reqq;	/* nfsreq queue - pending */
	int	nm_bioqlen;		/* number of buffers in queue */
	int	nm_reqqlen;		/* number of nfsreqs in queue */
	u_int64_t nm_maxfilesize;	/* maximum file size */
	struct ucred *nm_cred;		/* 'root' credential */
	struct lwkt_token nm_token;	/* protective token */
};


#if defined(_KERNEL)
/*
 * Convert mount ptr to nfsmount ptr.
 */
#define VFSTONFS(mp)	((struct nfsmount *)((mp)->mnt_data))
extern void nfs_free_mount(struct nfsmount *nmp);
extern void nfs_setvtype(struct vnode *, enum vtype);

#endif

#endif
