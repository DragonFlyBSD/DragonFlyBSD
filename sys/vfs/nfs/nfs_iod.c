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

#include <sys/signal2.h>
#include <sys/thread2.h>
#include <sys/mutex2.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include "rpcv2.h"
#include "nfsproto.h"
#include "nfs.h"
#include "xdr_subs.h"
#include "nfsm_subs.h"
#include "nfsmount.h"
#include "nfsnode.h"
#include "nfsrtt.h"

/*
 * nfs service connection reader thread
 */
void
nfssvc_iod_reader(void *arg)
{
	struct nfsmount *nmp = arg;
	struct nfsm_info *info;
	struct nfsreq *req;
	int error;

	lwkt_gettoken(&nmp->nm_token);

	if (nmp->nm_rxstate == NFSSVC_INIT)
		nmp->nm_rxstate = NFSSVC_PENDING;
	for (;;) {
		if (nmp->nm_rxstate == NFSSVC_WAITING) {
			if (TAILQ_FIRST(&nmp->nm_reqq) == NULL &&
			    TAILQ_FIRST(&nmp->nm_reqrxq) == NULL) {
				tsleep(&nmp->nm_rxstate, 0, "nfsidl", 0);
			} else {
				/*
				 * This can happen during shutdown, we don't
				 * want to hardloop.
				 */
				error = nfs_reply(nmp, NULL);
				if (error && error != EWOULDBLOCK) {
					tsleep(&nmp->nm_rxstate, 0,
						"nfsxxx", hz / 10);
				}
			}
			continue;
		}
		if (nmp->nm_rxstate != NFSSVC_PENDING)
			break;
		nmp->nm_rxstate = NFSSVC_WAITING;

		/*
		 * Process requests which have received replies.  Only
		 * process the post-reply states.  If we get EINPROGRESS
		 * it means the request went back to an auth or retransmit
		 * state and we let the iod_writer thread deal with it.
		 *
		 * Any lock on the request is strictly temporary due to
		 * MP races (XXX).
		 *
		 * If the request completes we run the info->done call
		 * to finish up the I/O.
		 */
		while ((req = TAILQ_FIRST(&nmp->nm_reqrxq)) != NULL) {
			if (req->r_flags & R_LOCKED) {
				while (req->r_flags & R_LOCKED) {
					req->r_flags |= R_WANTED;
					tsleep(req, 0, "nfstrac", 0);
				}
				continue;
			}
			TAILQ_REMOVE(&nmp->nm_reqrxq, req, r_chain);
			info = req->r_info;
			KKASSERT(info);
			info->error = nfs_request(info,
						  NFSM_STATE_PROCESSREPLY,
						  NFSM_STATE_DONE);
			if (info->error == EINPROGRESS) {
				kprintf("rxq: move info %p back to txq\n", info);
				TAILQ_INSERT_TAIL(&nmp->nm_reqtxq, req, r_chain);
				nfssvc_iod_writer_wakeup(nmp);
			} else {
				atomic_subtract_int(&nmp->nm_bioqlen, 1);
				info->done(info);
			}
		}
	}
	nmp->nm_rxthread = NULL;
	nmp->nm_rxstate = NFSSVC_DONE;

	lwkt_reltoken(&nmp->nm_token);
	wakeup(&nmp->nm_rxthread);
}

/*
 * nfs service connection writer thread
 *
 * The writer sits on the send side of the client's socket and
 * does both the initial processing of BIOs and also transmission
 * and retransmission of nfsreq's.
 *
 * The writer processes both new BIOs from nm_bioq and retransmit
 * or state machine jumpbacks from nm_reqtxq
 */
void
nfssvc_iod_writer(void *arg)
{
	struct nfsmount *nmp = arg;
	struct bio *bio;
	struct nfsreq *req;
	struct vnode *vp;
	nfsm_info_t info;

	lwkt_gettoken(&nmp->nm_token);

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

		/*
		 * Eep, we could blow out the mbuf allocator if we just
		 * did everything the kernel wanted us to do.
		 */
		while ((bio = TAILQ_FIRST(&nmp->nm_bioq)) != NULL) {
			if (nmp->nm_reqqlen > nfs_maxasyncbio)
				break;
			TAILQ_REMOVE(&nmp->nm_bioq, bio, bio_act);
			vp = bio->bio_driver_info;
			nfs_startio(vp, bio, NULL);
		}

		/*
		 * Process reauths & retransmits.  If we get an EINPROGRESS
		 * it means the state transitioned to WAITREPLY or later.
		 * Otherwise the request completed (probably with an error
		 * since we didn't get to a replied state).
		 */
		while ((req = TAILQ_FIRST(&nmp->nm_reqtxq)) != NULL) {
			TAILQ_REMOVE(&nmp->nm_reqtxq, req, r_chain);
			info = req->r_info;
			KKASSERT(info);
			info->error = nfs_request(info,
						  NFSM_STATE_AUTH,
						  NFSM_STATE_WAITREPLY);
			if (info->error == EINPROGRESS) {
				;
			} else {
				atomic_subtract_int(&nmp->nm_bioqlen, 1);
				info->done(info);
			}
		}
	}
	nmp->nm_txthread = NULL;
	nmp->nm_txstate = NFSSVC_DONE;
	lwkt_reltoken(&nmp->nm_token);
	wakeup(&nmp->nm_txthread);
}

void
nfssvc_iod_stop1(struct nfsmount *nmp)
{
	nmp->nm_txstate = NFSSVC_STOPPING;
	nmp->nm_rxstate = NFSSVC_STOPPING;
}

void
nfssvc_iod_stop2(struct nfsmount *nmp)
{
	wakeup(&nmp->nm_txstate);
	while (nmp->nm_txthread)
		tsleep(&nmp->nm_txthread, 0, "nfssttx", hz*2);
	wakeup(&nmp->nm_rxstate);
	while (nmp->nm_rxthread)
		tsleep(&nmp->nm_rxthread, 0, "nfsstrx", hz*2);
}

void
nfssvc_iod_writer_wakeup(struct nfsmount *nmp)
{
	if (nmp->nm_txstate == NFSSVC_WAITING) {
		nmp->nm_txstate = NFSSVC_PENDING;
		wakeup(&nmp->nm_txstate);
	}
}

void
nfssvc_iod_reader_wakeup(struct nfsmount *nmp)
{
	if (nmp->nm_rxstate == NFSSVC_WAITING) {
		nmp->nm_rxstate = NFSSVC_PENDING;
		wakeup(&nmp->nm_rxstate);
	}
}
