/*
 * Copyright (c) 1992, 1993
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
 *	@(#)nfs_nqlease.c	8.9 (Berkeley) 5/20/95
 * $FreeBSD: src/sys/nfs/nfs_nqlease.c,v 1.50 2000/02/13 03:32:05 peter Exp $
 * $DragonFly: src/sys/vfs/nfs/nfs_kerb.c,v 1.3 2006/09/05 00:55:50 dillon Exp $
 */

#include <sys/param.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/protosw.h>

#include <netinet/in.h>
#include "rpcv2.h"
#include "nfsproto.h"
#include "nfs.h"
#include "nfsm_subs.h"
#include "xdr_subs.h"
#include "nfsmount.h"
#include "nfsnode.h"

#include <sys/thread2.h>

#define TRUE	1
#define	FALSE	0

#ifndef NFS_NOSERVER 

/*
 * Nqnfs client helper daemon. Runs once a second to expire leases.
 * It also get authorization strings for "kerb" mounts.
 * It must start at the beginning of the list again after any potential
 * "sleep" since nfs_reclaim() called from vclean() can pull a node off
 * the list asynchronously.
 */
int
nfs_clientd(struct nfsmount *nmp, struct ucred *cred, struct nfsd_cargs *ncd,
	    int flag, caddr_t argp, struct thread *td)
{
	struct nfsuid *nuidp, *nnuidp;
	int error = 0;

	/*
	 * If an authorization string is being passed in, get it.
	 */
	if ((flag & NFSSVC_GOTAUTH) &&
	    (nmp->nm_state & (NFSSTA_WAITAUTH | NFSSTA_DISMNT)) == 0) {
	    if (nmp->nm_state & NFSSTA_HASAUTH)
		panic("cld kerb");
	    if ((flag & NFSSVC_AUTHINFAIL) == 0) {
		if (ncd->ncd_authlen <= nmp->nm_authlen &&
		    ncd->ncd_verflen <= nmp->nm_verflen &&
		    !copyin(ncd->ncd_authstr,nmp->nm_authstr,ncd->ncd_authlen)&&
		    !copyin(ncd->ncd_verfstr,nmp->nm_verfstr,ncd->ncd_verflen)){
		    nmp->nm_authtype = ncd->ncd_authtype;
		    nmp->nm_authlen = ncd->ncd_authlen;
		    nmp->nm_verflen = ncd->ncd_verflen;
#ifdef NFSKERB
		    nmp->nm_key = ncd->ncd_key;
#endif
		} else
		    nmp->nm_state |= NFSSTA_AUTHERR;
	    } else
		nmp->nm_state |= NFSSTA_AUTHERR;
	    nmp->nm_state |= NFSSTA_HASAUTH;
	    wakeup((caddr_t)&nmp->nm_authlen);
	} else
	    nmp->nm_state |= NFSSTA_WAITAUTH;

	/*
	 * Loop every second updating queue until there is a termination sig.
	 */
	while ((nmp->nm_state & NFSSTA_DISMNT) == 0) {
	    /*
	     * Get an authorization string, if required.
	     */
	    if ((nmp->nm_state & (NFSSTA_WAITAUTH | NFSSTA_DISMNT | NFSSTA_HASAUTH)) == 0) {
		ncd->ncd_authuid = nmp->nm_authuid;
		if (copyout((caddr_t)ncd, argp, sizeof (struct nfsd_cargs)))
			nmp->nm_state |= NFSSTA_WAITAUTH;
		else
			return (ENEEDAUTH);
	    }

	    /*
	     * Wait a bit (no pun) and do it again.
	     */
	    if ((nmp->nm_state & NFSSTA_DISMNT) == 0 &&
		(nmp->nm_state & (NFSSTA_WAITAUTH | NFSSTA_HASAUTH))) {
		    error = tsleep((caddr_t)&nmp->nm_authstr, PCATCH,
			"nqnfstimr", hz / 3);
		    if (error == EINTR || error == ERESTART)
			(void) dounmount(nmp->nm_mountp, 0);
	    }
	}

	/*
	 * Finally, we can free up the mount structure.
	 */
	TAILQ_FOREACH_MUTABLE(nuidp, &nmp->nm_uidlruhead, nu_lru, nnuidp) {
		LIST_REMOVE(nuidp, nu_hash);
		TAILQ_REMOVE(&nmp->nm_uidlruhead, nuidp, nu_lru);
		kfree((caddr_t)nuidp, M_NFSUID);
	}
	nfs_free_mount(nmp);
	if (error == EWOULDBLOCK)
		error = 0;
	return (error);
}

#endif /* NFS_NOSERVER */
