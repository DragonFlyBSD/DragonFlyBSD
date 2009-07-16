/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
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
 */
/*
 * NFSIOD operations - now built into the kernel.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/vnode.h>
#include <sys/fcntl.h>
#include <sys/protosw.h>
#include <sys/resourcevar.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/socketops.h>
#include <sys/syslog.h>
#include <sys/thread.h>
#include <sys/tprintf.h>
#include <sys/sysctl.h>
#include <sys/signalvar.h>
#include <sys/mutex.h>

#include <sys/signal2.h>
#include <sys/mutex2.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/thread2.h>

#include "rpcv2.h"
#include "nfsproto.h"
#include "nfs.h"
#include "xdr_subs.h"
#include "nfsm_subs.h"
#include "nfsmount.h"
#include "nfsnode.h"
#include "nfsrtt.h"

void
nfssvc_iod_reader(void *arg)
{
	struct nfsmount *nmp = arg;

	if (nmp->nm_rxstate == NFSSVC_INIT)
		nmp->nm_rxstate = NFSSVC_PENDING;
	for (;;) {
		if (nmp->nm_rxstate == NFSSVC_WAITING) {
			tsleep(&nmp->nm_rxstate, 0, "nfsidl", 0);
			continue;
		}
		if (nmp->nm_rxstate != NFSSVC_PENDING)
			break;
		nmp->nm_rxstate = NFSSVC_WAITING;

#if 0
		error = tsleep((caddr_t)&nfs_iodwant[myiod],
			PCATCH, "nfsidl", 0);
#endif
	}
	nmp->nm_rxthread = NULL;
	nmp->nm_rxstate = NFSSVC_DONE;
	wakeup(&nmp->nm_rxthread);
}

/*
 * The writer sits on the send side of the client's socket and
 * does both the initial processing of BIOs and also transmission
 * and retransmission of nfsreq's.
 */
void
nfssvc_iod_writer(void *arg)
{
	struct nfsmount *nmp = arg;
	struct bio *bio;
	struct vnode *vp;

	if (nmp->nm_txstate == NFSSVC_INIT)
		nmp->nm_txstate = NFSSVC_PENDING;
	for (;;) {
		if (nmp->nm_txstate == NFSSVC_WAITING) {
			tsleep(&nmp->nm_txstate, 0, "nfsidl", 0);
			continue;
		}
		if (nmp->nm_txstate != NFSSVC_PENDING)
			break;
		nmp->nm_txstate = NFSSVC_WAITING;

		while (nmp->nm_bioqlen && nmp->nm_reqqlen < 32) {
			bio = TAILQ_FIRST(&nmp->nm_bioq);
			KKASSERT(bio);
			TAILQ_REMOVE(&nmp->nm_bioq, bio, bio_act);
			nmp->nm_bioqlen--;
			vp = bio->bio_driver_info;
			nfs_doio(vp, bio, NULL);
		}
	}
	nmp->nm_txthread = NULL;
	nmp->nm_txstate = NFSSVC_DONE;
	wakeup(&nmp->nm_txthread);
}

void
nfssvc_iod_stop(struct nfsmount *nmp)
{
	nmp->nm_txstate = NFSSVC_STOPPING;
	wakeup(&nmp->nm_txstate);
	while (nmp->nm_txthread)
		tsleep(&nmp->nm_txthread, 0, "nfssttx", 0);

	nmp->nm_rxstate = NFSSVC_STOPPING;
	wakeup(&nmp->nm_rxstate);
	while (nmp->nm_rxthread)
		tsleep(&nmp->nm_rxthread, 0, "nfsstrx", 0);
}

void
nfssvc_iod_writer_wakeup(struct nfsmount *nmp)
{
	if (nmp->nm_txstate == NFSSVC_WAITING) {
		nmp->nm_txstate = NFSSVC_PENDING;
		wakeup(&nmp->nm_txstate);
	}
}
