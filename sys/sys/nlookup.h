/*
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $DragonFly: src/sys/sys/nlookup.h,v 1.6 2008/05/09 17:52:18 dillon Exp $
 */

#ifndef _SYS_NLOOKUP_H_
#define	_SYS_NLOOKUP_H_

#ifndef _SYS_UIO_H_
#include <sys/uio.h>
#endif
#ifndef _SYS_NAMECACHE_H_
#include <sys/namecache.h>
#endif
#ifndef _SYS_FILE_H_
#include <sys/file.h>
#endif

struct vnode;
struct vattr;
struct mount;
struct thread;
struct ucred;

/*
 * nlookup component
 */
struct nlcomponent {
	char 		*nlc_nameptr;
	int		nlc_namelen;
};

/*
 * Encapsulation of nlookup parameters.
 *
 * Note on nl_flags and nl_op: nl_flags supports a simplified subset of 
 * namei's original CNP flags.  nl_op (e.g. NAMEI_*) does no in any way
 * effect the state of the returned namecache and is only used to enforce
 * access checks.
 */
struct nlookupdata {
	/*
	 * These fields are setup by nlookup_init() with nl_nch set to
	 * the current directory if a process or the root directory if
	 * a pure thread.  The result from nlookup() will be returned in
	 * nl_nch.
	 */
	struct nchandle nl_nch;		/* start-point and result */
	struct nchandle nl_rootnch;	/* root directory */
	struct nchandle nl_jailnch;	/* jail directory */

	char 		*nl_path;	/* path buffer */
	struct thread	*nl_td;		/* thread requesting the nlookup */
	struct ucred	*nl_cred;	/* credentials for nlookup */
	struct vnode	*nl_dvp;	/* NLC_REFDVP */

	int		nl_flags;	/* operations flags */
	int		nl_loopcnt;	/* symlinks encountered */

	/*
	 * These fields are populated by vn_open().  nlookup_done() will
	 * vn_close() a non-NULL vp so if you extract it be sure to NULL out
	 * nl_open_vp.
	 */
	struct  vnode	*nl_open_vp;	
	int		nl_vp_fmode;
};

/*
 * NOTE: nlookup() flags related to open checks do not actually perform
 *	 any modifying operation.  e.g. the file isn't created, truncated,
 *	 etc.  vn_open() handles that.
 */
#define NLC_FOLLOW		0x00000001	/* follow leaf symlink */
#define NLC_NOCROSSMOUNT	0x00000002	/* do not cross mount points */
#define NLC_HASBUF		0x00000004	/* nl_path is allocated */
#define NLC_ISWHITEOUT		0x00000008
#define NLC_WILLBEDIR		0x00000010
#define NLC_NCPISLOCKED		0x00000020
#define NLC_LOCKVP		0x00000040	/* nl_open_vp from vn_open */
#define NLC_CREATE		0x00000080	/* do create checks */
#define NLC_DELETE		0x00000100	/* do delete checks */
#define NLC_RENAME_DST		0x00000200	/* do rename checks (target) */
#define NLC_OPEN		0x00000400	/* do open checks */
#define NLC_TRUNCATE		0x00000800	/* do truncation checks */
#define NLC_HLINK		0x00001000	/* do hardlink checks */
#define NLC_RENAME_SRC		0x00002000	/* do rename checks (source) */
#define NLC_SHAREDLOCK		0x00004000	/* allow shared ncp & vp lock */
#define NLC_UNUSED00008000	0x00008000
#define NLC_NFS_RDONLY		0x00010000	/* set by nfs_namei() only */
#define NLC_NFS_NOSOFTLINKTRAV	0x00020000	/* do not traverse softlnks */
#define NLC_REFDVP		0x00040000	/* set ref'd/unlocked nl_dvp */

#define NLC_APPEND		0x00100000	/* open check: append */
#define NLC_UNUSED00200000	0x00200000

#define NLC_READ		0x00400000	/* require read access */
#define NLC_WRITE		0x00800000	/* require write access */
#define NLC_EXEC		0x01000000	/* require execute access */
#define NLC_EXCL		0x02000000	/* open check: exclusive */
#define NLC_OWN			0x04000000	/* open check: owner override */
#define NLC_UNUSED08000000	0x08000000
#define NLC_STICKY		0x10000000	/* indicate sticky case */
#define NLC_APPENDONLY		0x20000000	/* indicate append-only */
#define NLC_IMMUTABLE		0x40000000	/* indicate immutable set */
#define NLC_WRITABLE		0x80000000	/* indicate writeable */

/*
 * All checks.  If any of these bits are set general user/group/world
 * permission checks will be done by nlookup().
 */
#define NLC_ALLCHKS		(NLC_CREATE | NLC_DELETE | NLC_RENAME_DST | \
				 NLC_OPEN | NLC_TRUNCATE | NLC_RENAME_SRC | \
				 NLC_READ | NLC_WRITE | NLC_EXEC | NLC_OWN)

#ifdef _KERNEL

int nlookup_init(struct nlookupdata *, const char *, enum uio_seg, int);
int nlookup_init_at(struct nlookupdata *, struct file **, int, const char *, 
		enum uio_seg, int);
int nlookup_init_raw(struct nlookupdata *, const char *, enum uio_seg, int, struct ucred *, struct nchandle *);
int nlookup_init_root(struct nlookupdata *, const char *, enum uio_seg, int, struct ucred *, struct nchandle *, struct nchandle *);
void nlookup_set_cred(struct nlookupdata *nd, struct ucred *cred);
void nlookup_zero(struct nlookupdata *);
void nlookup_done(struct nlookupdata *);
void nlookup_done_at(struct nlookupdata *, struct file *);
struct nchandle nlookup_simple(const char *str, enum uio_seg seg, 
				int niflags, int *error);
int nlookup_mp(struct mount *mp, struct nchandle *nch);
int nlookup(struct nlookupdata *);
int nreadsymlink(struct nlookupdata *nd, struct nchandle *nch, 
				struct nlcomponent *nlc);
int naccess_va(struct vattr *va, int nflags, struct ucred *cred);

#endif

#endif /* !_SYS_NAMEI_H_ */
