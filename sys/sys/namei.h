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
 *	@(#)namei.h	8.5 (Berkeley) 1/9/95
 * $FreeBSD: src/sys/sys/namei.h,v 1.29.2.2 2001/09/30 21:12:54 luigi Exp $
 * $DragonFly: src/sys/sys/namei.h,v 1.15 2004/11/12 00:09:27 dillon Exp $
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
	char	*cn_nameptr;	/* pointer to looked up name */
	long	cn_namelen;	/* length of looked up component */
	long	cn_consume;	/* chars to consume in lookup() */
	int	cn_timeout;	/* if CNP_CACHETIMEOUT is set, in ticks */
	struct vnode *cn_notvp;	/* used by NFS to check for collision */
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
	/* (LOCKLEAF)	    0x00000004	*/
#define	CNP_LOCKPARENT	    0x00000008	/* return parent vnode locked */
#define	CNP_WANTPARENT	    0x00000010	/* return parent vnode unlocked */
	/* (NOCACHE)	    0x00000020	*/
#define	CNP_FOLLOW	    0x00000040	/* follow symbolic links */
	/* (NOOBJ)	    0x00000080	*/
#define	CNP_MODMASK	    0x00c000fc	/* mask of operational modifiers */
/*
 * Namei parameter descriptors.
 */
	/* (NOCROSSMOUNT)   0x00000100 */
#define	CNP_RDONLY	    0x00000200 /* lookup with read-only semantics */
#define CNP_NOTVP	    0x00000400 /* test cn_notvp, fail if matches */
	/* (SAVENAME)	    0x00000800 */
	/* (CNP_SAVESTART)  0x00001000 */
#define CNP_ISDOTDOT	    0x00002000 /* current component name is .. */
	/* (MAKEENTRY)	    0x00004000 */
	/* (ISLASTCN)	    0x00008000 */
	/* (ISSYMLINK)	    0x00010000 */
#define	CNP_ISWHITEOUT	    0x00020000 /* found whiteout */
#define	CNP_DOWHITEOUT	    0x00040000 /* do whiteouts */
	/* (WILLBEDIR)	    0x00080000 */
	/* (ISUNICODE)	    0x00100000 */
#define	CNP_PDIRUNLOCK	    0x00200000 /* fs lookup() unlocked parent dir */
	/* (WANTDNCP)	    0x00400000 */
	/* (WANTNCP)	    0x00800000 */
	/* (CACHETIMEOUT)   0x01000000 */
#define CNP_PARAMASK	    0x011fff00 /* mask of parameter descriptors */

extern int varsym_enable;

int	relookup (struct vnode *dvp, struct vnode **vpp,
	    struct componentname *cnp);
#endif

#endif /* !_SYS_NAMEI_H_ */
