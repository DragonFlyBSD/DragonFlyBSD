/*
 * Copyright (c) 1985, 1989, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)namei.h	8.5 (Berkeley) 1/9/95
 * $FreeBSD: src/sys/sys/namei.h,v 1.29.2.2 2001/09/30 21:12:54 luigi Exp $
 * $DragonFly: src/sys/sys/namei.h,v 1.12 2004/04/08 22:00:40 dillon Exp $
 */

#ifndef _SYS_NAMEI_H_
#define	_SYS_NAMEI_H_

#include <sys/queue.h>
#include <sys/uio.h>

#ifdef _KERNEL
#ifndef _SYS_THREAD_H_
#include <sys/thread.h>
#endif
#ifndef _SYS_PROC_H_
#include <sys/proc.h>
#endif
#endif

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)
#ifndef _SYS_NCHSTATS_H_
#include <sys/nchstats.h>
#endif
#endif

struct componentname {
	/*
	 * Arguments to lookup.
	 */
	u_long	cn_nameiop;	/* namei operation */
	u_long	cn_flags;	/* flags to namei */
	struct	thread *cn_td;	/* process requesting lookup */
	struct	ucred *cn_cred;	/* credentials */
	/*
	 * Shared between lookup and commit routines.
	 */
	char	*cn_pnbuf;	/* pathname buffer */
	char	*cn_nameptr;	/* pointer to looked up name */
	long	cn_namelen;	/* length of looked up component */
	long	cn_consume;	/* chars to consume in lookup() */
	int	cn_timeout;	/* if CNP_CACHETIMEOUT is set, in ticks */
};

/*
 * Encapsulation of namei parameters.
 */
struct nameidata {
	/*
	 * Arguments to namei/lookup.
	 */
	const	char *ni_dirp;		/* pathname pointer */
	enum	uio_seg ni_segflg;	/* location of pathname */
	/*
	 * Arguments to lookup.
	 */
	struct	vnode *ni_startdir;	/* starting directory */
	struct	vnode *ni_rootdir;	/* logical root directory */
	struct	vnode *ni_topdir;	/* logical top directory */
	/*
	 * Results: returned from/manipulated by lookup
	 */
	struct	vnode *ni_vp;		/* vnode of result */
	struct	vnode *ni_dvp;		/* vnode of intermediate directory */
	struct  namecache *ni_ncp;	/* namecache of result */
	struct  namecache *ni_dncp;	/* namecache of intermediate dir */
	/*
	 * Shared between namei and lookup/commit routines.
	 */
	size_t	ni_pathlen;		/* remaining chars in path incl \0 */
	char	*ni_next;		/* next location in pathname */
	u_long	ni_loopcnt;		/* count of symlinks encountered */
	/*
	 * Lookup parameters: this structure describes the subset of
	 * information from the nameidata structure that is passed
	 * through the VOP interface.
	 */
	struct componentname ni_cnd;
};

#ifdef _KERNEL
/*
 * namei operations
 */
#define	NAMEI_LOOKUP	0	/* perform name lookup only */
#define	NAMEI_CREATE	1	/* setup for file creation */
#define	NAMEI_DELETE	2	/* setup for file deletion */
#define	NAMEI_RENAME	3	/* setup for file renaming */
#define	NAMEI_OPMASK	3	/* mask for operation */
/*
 * namei operational modifier flags, stored in ni_cnd.flags
 */
#define	CNP_LOCKLEAF	    0x00000004	/* return target vnode locked */
#define	CNP_LOCKPARENT	    0x00000008	/* return parent vnode locked */
#define	CNP_WANTPARENT	    0x00000010	/* return parent vnode unlocked */
#define	CNP_NOCACHE	    0x00000020	/* name must not be left in cache */
#define	CNP_FOLLOW	    0x00000040	/* follow symbolic links */
#define	CNP_NOOBJ	    0x00000080	/* don't create object */
#define	CNP_WANTDNCP	    0x00400000	/* return target namecache refd */
#define	CNP_WANTNCP	    0x00800000	/* return target namecache refd */
#define	CNP_MODMASK	    0x00c000fc	/* mask of operational modifiers */
/*
 * Namei parameter descriptors.
 *
 * SAVENAME may be set by either the callers of namei or by VOP_LOOKUP.
 * If the caller of namei sets the flag (for example execve wants to
 * know the name of the program that is being executed), then it must
 * free the buffer. If VOP_LOOKUP sets the flag, then the buffer must
 * be freed by either the commit routine or the VOP_ABORT routine.
 * SAVESTART is set only by the callers of namei. It implies SAVENAME
 * plus the addition of saving the parent directory that contains the
 * name in ni_startdir. It allows repeated calls to lookup for the
 * name being sought. The caller is responsible for releasing the
 * buffer and for vrele'ing ni_startdir.
 */
#define	CNP_NOCROSSMOUNT    0x00000100 /* do not cross mount points */
#define	CNP_RDONLY	    0x00000200 /* lookup with read-only semantics */
#define	CNP_HASBUF	    0x00000400 /* has allocated pathname buffer */
#define	CNP_SAVENAME	    0x00000800 /* save pathname buffer */
#define	CNP_SAVESTART	    0x00001000 /* save starting directory */
#define CNP_ISDOTDOT	    0x00002000 /* current component name is .. */
#define CNP_MAKEENTRY	    0x00004000 /* entry will be added to name cache */
#define CNP_ISLASTCN	    0x00008000 /* flag last component of pathname */
#define CNP_ISSYMLINK	    0x00010000 /* symlink needs interpretation */
#define	CNP_ISWHITEOUT	    0x00020000 /* found whiteout */
#define	CNP_DOWHITEOUT	    0x00040000 /* do whiteouts */
#define	CNP_WILLBEDIR	    0x00080000 /* will be dir, allow trailing / */
#define	CNP_ISUNICODE	    0x00100000 /* current component name is unicode*/
#define	CNP_PDIRUNLOCK	    0x00200000 /* fs lookup() unlocked parent dir */
	/* (WANTDNCP)	    0x00400000 */
	/* (WANTNCP)	    0x00800000 */
#define CNP_CACHETIMEOUT    0x01000000 /* apply timeout to cache entry */
#define CNP_PARAMASK	    0x011fff00 /* mask of parameter descriptors */

/*
 * Initialization of an nameidata structure.
 */

static __inline void
_NDINIT(struct nameidata *ndp, u_long op, u_long flags, enum uio_seg segflg,
	const char *namep, struct thread *td
) {
	ndp->ni_cnd.cn_nameiop = op;
	ndp->ni_cnd.cn_flags = flags;
	ndp->ni_segflg = segflg;
	ndp->ni_dirp = namep;
	ndp->ni_cnd.cn_td = td;
}

static __inline void
NDINIT(struct nameidata *ndp, u_long op, u_long flags, enum uio_seg segflg,
	const char *namep, struct thread *td
) {
	struct proc *p;

	_NDINIT(ndp, op, flags, segflg, namep, td);
	p = td->td_proc;
	KKASSERT(p != NULL);
	ndp->ni_cnd.cn_cred = p->p_ucred;
}

static __inline void
NDINIT2(struct nameidata *ndp, u_long op, u_long flags, enum uio_seg segflg,
	const char *namep, struct thread *td, struct ucred *cr
) {
	_NDINIT(ndp, op, flags, segflg, namep, td);
	ndp->ni_cnd.cn_cred = cr;
}

#define NDF_NO_DVP_RELE		0x00000001
#define NDF_NO_DVP_UNLOCK	0x00000002
#define NDF_NO_DVP_PUT		0x00000003
#define NDF_NO_VP_RELE		0x00000004
#define NDF_NO_VP_UNLOCK	0x00000008
#define NDF_NO_VP_PUT		(NDF_NO_VP_RELE|NDF_NO_VP_UNLOCK)
#define NDF_NO_STARTDIR_RELE	0x00000010
#define NDF_NO_FREE_PNBUF	0x00000020
#define NDF_NO_DNCP_RELE	0x00000040
#define NDF_NO_NCP_RELE		0x00000080

#define NDF_ONLY_PNBUF		(~NDF_NO_FREE_PNBUF)
#define NDF_ONLY_PNBUF_AND_NCPS	(~(NDF_NO_FREE_PNBUF|NDF_NO_DNCP_RELE|NDF_NO_NCP_RELE))

void NDFREE (struct nameidata *, const uint);

int	namei (struct nameidata *ndp);
int	lookup (struct nameidata *ndp);
int	relookup (struct vnode *dvp, struct vnode **vpp,
	    struct componentname *cnp);
#endif

#endif /* !_SYS_NAMEI_H_ */
