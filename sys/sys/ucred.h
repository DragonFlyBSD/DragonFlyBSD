/*
 * Copyright (c) 1989, 1993
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
 *	@(#)ucred.h	8.4 (Berkeley) 1/9/95
 * $FreeBSD: src/sys/sys/ucred.h,v 1.14.2.5 2002/03/09 05:20:25 dd Exp $
 * $DragonFly: src/sys/sys/ucred.h,v 1.9 2007/01/08 21:32:57 corecode Exp $
 */

#ifndef _SYS_UCRED_H_
#define	_SYS_UCRED_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_PARAM_H_
#include <sys/param.h>
#endif
#ifndef _SYS_SPINLOCK_H_
#include <sys/spinlock.h>
#endif

struct prison;

/*
 * Credentials.
 *
 * Please do not inspect cr_uid directly to determine superuserness.
 * Only the priv(9) functions should be used for this.
 */
struct ucred {
	int	cr_ref;			/* reference count */
	uid_t	cr_uid;			/* effective user id */
	short	cr_ngroups;		/* number of groups */
	gid_t	cr_groups[NGROUPS];	/* groups */
	struct	uidinfo *cr_uidinfo;	/* per uid resource consumption */
	struct	uidinfo *cr_ruidinfo;	/* per ruid resource consumption */
	struct	prison *cr_prison;	/* prison info */
	uid_t   cr_ruid;		/* Real user id. */
	uid_t   cr_svuid;		/* Saved effective user id. */
	gid_t   cr_rgid;		/* Real group id. */
	gid_t   cr_svgid;		/* Saved effective group id. */
};
#define cr_gid cr_groups[0]
#define NOCRED ((struct ucred *)0)	/* no credential available */
#define FSCRED ((struct ucred *)-1)	/* filesystem credential */

/*
 * This is the external representation of struct ucred, based upon the
 * size of a 4.2-RELEASE struct ucred.  There will probably never be
 * any need to change the size of this or layout of its used fields.
 */
struct xucred {
	u_int	cr_version;		/* structure layout version */
	uid_t	cr_uid;			/* effective user id */
	short	cr_ngroups;		/* number of groups */
	gid_t	cr_groups[NGROUPS];	/* groups */
	void	*_cr_unused1;		/* compatibility with old ucred */
};
#define	XUCRED_VERSION	0

#ifdef _KERNEL

struct proc;

struct ucred	*change_euid (uid_t euid);
struct ucred	*change_ruid (uid_t ruid);
struct ucred	*cratom (struct ucred **pcr);
struct ucred	*cratom_proc (struct proc *p);
struct ucred	*crcopy (struct ucred *cr);
struct ucred	*crdup (struct ucred *cr);
void		crfree (struct ucred *cr);
void		crinit (struct ucred *cr);
struct ucred	*crget (void);
struct ucred    *crhold (struct ucred *cr);
void		cru2x (struct ucred *cr, struct xucred *xcr);
int		groupmember (gid_t gid, struct ucred *cred);
#endif /* _KERNEL */

#endif /* !_SYS_UCRED_H_ */
