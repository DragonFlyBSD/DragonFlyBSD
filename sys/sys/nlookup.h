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
 * $DragonFly: src/sys/sys/nlookup.h,v 1.1 2004/09/28 00:25:31 dillon Exp $
 */

#ifndef _SYS_NLOOKUP_H_
#define	_SYS_NLOOKUP_H_

/*
 * nlookup component
 */
struct nlcomponent {
	char 		*nlc_nameptr;
	int		nlc_namelen;
};

/*
 * Encapsulation of nlookup parameters
 */
struct nlookupdata {
	struct namecache *nl_ncp;	/* start-point and result */
	struct namecache *nl_rootncp;	/* root directory */
	struct namecache *nl_jailncp;	/* jail directory */

	char 		*nl_path;	/* path buffer */
	struct thread	*nl_td;		/* thread requesting the nlookup */
	struct ucred	*nl_cred;	/* credentials for nlookup */

	int		nl_flags;	/* operations flags */
	int		nl_loopcnt;	/* symlinks encountered */
};

#define NLC_FOLLOW		CNP_FOLLOW
#define NLC_NOCROSSMOUNT	CNP_NOCROSSMOUNT
#define NLC_HASBUF		CNP_HASBUF
#define NLC_ISWHITEOUT		CNP_ISWHITEOUT
#define NLC_WILLBEDIR		CNP_WILLBEDIR
#define NLC_NCPISLOCKED		CNP_LOCKLEAF

#ifdef _KERNEL

int nlookup_init(struct nlookupdata *, const char *, enum uio_seg, int);
void nlookup_done(struct nlookupdata *);
struct namecache *nlookup_simple(const char *str, enum uio_seg seg, 
				int niflags, int *error);
int nlookup(struct nlookupdata *);
int nreadsymlink(struct nlookupdata *nd, struct namecache *ncp, 
				struct nlcomponent *nlc);


#endif

#endif /* !_SYS_NAMEI_H_ */
