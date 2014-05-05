/*
 * Copyright (c) 2011-2012 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
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

#include "dmsg_local.h"

int DMsgDebugOpt;

static int dmsg_state_msgrx(dmsg_msg_t *msg);
static void dmsg_state_cleanuptx(dmsg_iocom_t *iocom, dmsg_msg_t *msg);
static void dmsg_msg_free_locked(dmsg_msg_t *msg);

RB_GENERATE(dmsg_state_tree, dmsg_state, rbnode, dmsg_state_cmp);

/*
 * STATE TREE - Represents open transactions which are indexed by their
 *		{ msgid } relative to the governing iocom.
 */
int
dmsg_state_cmp(dmsg_state_t *state1, dmsg_state_t *state2)
{
	if (state1->msgid < state2->msgid)
		return(-1);
	if (state1->msgid > state2->msgid)
		return(1);
	return(0);
}

/*
 * Initialize a low-level ioq
 */
void
dmsg_ioq_init(dmsg_iocom_t *iocom __unused, dmsg_ioq_t *ioq)
{
	bzero(ioq, sizeof(*ioq));
	ioq->state = DMSG_MSGQ_STATE_HEADER1;
	TAILQ_INIT(&ioq->msgq);
}

/*
 * Cleanup queue.
 *
 * caller holds iocom->mtx.
 */
void
dmsg_ioq_done(dmsg_iocom_t *iocom __unused, dmsg_ioq_t *ioq)
{
	dmsg_msg_t *msg;

	while ((msg = TAILQ_FIRST(&ioq->msgq)) != NULL) {
		assert(0);	/* shouldn't happen */
		TAILQ_REMOVE(&ioq->msgq, msg, qentry);
		dmsg_msg_free(msg);
	}
	if ((msg = ioq->msg) != NULL) {
		ioq->msg = NULL;
		dmsg_msg_free(msg);
	}
}

/*
 * Initialize a low-level communications channel.
 *
 * NOTE: The signal_func() is called at least once from the loop and can be
 *	 re-armed via dmsg_iocom_restate().
 */
void
dmsg_iocom_init(dmsg_iocom_t *iocom, int sock_fd, int alt_fd,
		   void (*signal_func)(dmsg_iocom_t *iocom),
		   void (*rcvmsg_func)(dmsg_msg_t *msg),
		   void (*usrmsg_func)(dmsg_msg_t *msg, int unmanaged),
		   void (*altmsg_func)(dmsg_iocom_t *iocom))
{
	struct stat st;

	bzero(iocom, sizeof(*iocom));

	asprintf(&iocom->label, "iocom-%p", iocom);
	iocom->signal_callback = signal_func;
	iocom->rcvmsg_callback = rcvmsg_func;
	iocom->altmsg_callback = altmsg_func;
	iocom->usrmsg_callback = usrmsg_func;

	pthread_mutex_init(&iocom->mtx, NULL);
	RB_INIT(&iocom->staterd_tree);
	RB_INIT(&iocom->statewr_tree);
	TAILQ_INIT(&iocom->freeq);
	TAILQ_INIT(&iocom->freeq_aux);
	TAILQ_INIT(&iocom->txmsgq);
	iocom->sock_fd = sock_fd;
	iocom->alt_fd = alt_fd;
	iocom->flags = DMSG_IOCOMF_RREQ | DMSG_IOCOMF_CLOSEALT;
	if (signal_func)
		iocom->flags |= DMSG_IOCOMF_SWORK;
	dmsg_ioq_init(iocom, &iocom->ioq_rx);
	dmsg_ioq_init(iocom, &iocom->ioq_tx);
	iocom->state0.iocom = iocom;
	iocom->state0.parent = &iocom->state0;
	iocom->state0.flags = DMSG_STATE_ROOT;
	TAILQ_INIT(&iocom->state0.subq);

	if (pipe(iocom->wakeupfds) < 0)
		assert(0);
	fcntl(iocom->wakeupfds[0], F_SETFL, O_NONBLOCK);
	fcntl(iocom->wakeupfds[1], F_SETFL, O_NONBLOCK);

	/*
	 * Negotiate session crypto synchronously.  This will mark the
	 * connection as error'd if it fails.  If this is a pipe it's
	 * a linkage that we set up ourselves to the filesystem and there
	 * is no crypto.
	 */
	if (fstat(sock_fd, &st) < 0)
		assert(0);
	if (S_ISSOCK(st.st_mode))
		dmsg_crypto_negotiate(iocom);

	/*
	 * Make sure our fds are set to non-blocking for the iocom core.
	 */
	if (sock_fd >= 0)
		fcntl(sock_fd, F_SETFL, O_NONBLOCK);
#if 0
	/* if line buffered our single fgets() should be fine */
	if (alt_fd >= 0)
		fcntl(alt_fd, F_SETFL, O_NONBLOCK);
#endif
}

void
dmsg_iocom_label(dmsg_iocom_t *iocom, const char *ctl, ...)
{
	va_list va;
	char *optr;

	va_start(va, ctl);
	optr = iocom->label;
	vasprintf(&iocom->label, ctl, va);
	va_end(va);
	if (optr)
		free(optr);
}

/*
 * May only be called from a callback from iocom_core.
 *
 * Adjust state machine functions, set flags to guarantee that both
 * the recevmsg_func and the sendmsg_func is called at least once.
 */
void
dmsg_iocom_restate(dmsg_iocom_t *iocom,
		   void (*signal_func)(dmsg_iocom_t *),
		   void (*rcvmsg_func)(dmsg_msg_t *msg))
{
	pthread_mutex_lock(&iocom->mtx);
	iocom->signal_callback = signal_func;
	iocom->rcvmsg_callback = rcvmsg_func;
	if (signal_func)
		atomic_set_int(&iocom->flags, DMSG_IOCOMF_SWORK);
	else
		atomic_clear_int(&iocom->flags, DMSG_IOCOMF_SWORK);
	pthread_mutex_unlock(&iocom->mtx);
}

void
dmsg_iocom_signal(dmsg_iocom_t *iocom)
{
	pthread_mutex_lock(&iocom->mtx);
	if (iocom->signal_callback)
		atomic_set_int(&iocom->flags, DMSG_IOCOMF_SWORK);
	pthread_mutex_unlock(&iocom->mtx);
}

/*
 * Cleanup a terminating iocom.
 *
 * Caller should not hold iocom->mtx.  The iocom has already been disconnected
 * from all possible references to it.
 */
void
dmsg_iocom_done(dmsg_iocom_t *iocom)
{
	dmsg_msg_t *msg;

	if (iocom->sock_fd >= 0) {
		close(iocom->sock_fd);
		iocom->sock_fd = -1;
	}
	if (iocom->alt_fd >= 0 && (iocom->flags & DMSG_IOCOMF_CLOSEALT)) {
		close(iocom->alt_fd);
		iocom->alt_fd = -1;
	}
	dmsg_ioq_done(iocom, &iocom->ioq_rx);
	dmsg_ioq_done(iocom, &iocom->ioq_tx);
	while ((msg = TAILQ_FIRST(&iocom->freeq)) != NULL) {
		TAILQ_REMOVE(&iocom->freeq, msg, qentry);
		free(msg);
	}
	while ((msg = TAILQ_FIRST(&iocom->freeq_aux)) != NULL) {
		TAILQ_REMOVE(&iocom->freeq_aux, msg, qentry);
		free(msg->aux_data);
		msg->aux_data = NULL;
		free(msg);
	}
	if (iocom->wakeupfds[0] >= 0) {
		close(iocom->wakeupfds[0]);
		iocom->wakeupfds[0] = -1;
	}
	if (iocom->wakeupfds[1] >= 0) {
		close(iocom->wakeupfds[1]);
		iocom->wakeupfds[1] = -1;
	}
	pthread_mutex_destroy(&iocom->mtx);
}

/*
 * Allocate a new message using the specified transaction state.
 *
 * If CREATE is set a new transaction is allocated relative to the passed-in
 * transaction (the 'state' argument becomes pstate).
 *
 * If CREATE is not set the message is associated with the passed-in
 * transaction.
 */
dmsg_msg_t *
dmsg_msg_alloc(dmsg_state_t *state,
	       size_t aux_size, uint32_t cmd,
	       void (*func)(dmsg_msg_t *), void *data)
{
	dmsg_iocom_t *iocom = state->iocom;
	dmsg_state_t *pstate;
	dmsg_msg_t *msg;
	int hbytes;
	size_t aligned_size;

	pthread_mutex_lock(&iocom->mtx);
#if 0
	if (aux_size) {
		aligned_size = DMSG_DOALIGN(aux_size);
		if ((msg = TAILQ_FIRST(&iocom->freeq_aux)) != NULL)
			TAILQ_REMOVE(&iocom->freeq_aux, msg, qentry);
	} else {
		aligned_size = 0;
		if ((msg = TAILQ_FIRST(&iocom->freeq)) != NULL)
			TAILQ_REMOVE(&iocom->freeq, msg, qentry);
	}
#endif
	aligned_size = DMSG_DOALIGN(aux_size);
	msg = NULL;
	if ((cmd & (DMSGF_CREATE | DMSGF_REPLY)) == DMSGF_CREATE) {
		/*
		 * When CREATE is set without REPLY the caller is
		 * initiating a new transaction stacked under the specified
		 * circuit.
		 *
		 * NOTE: CREATE in txcmd handled by dmsg_msg_write()
		 * NOTE: DELETE in txcmd handled by dmsg_state_cleanuptx()
		 */
		pstate = state;
		state = malloc(sizeof(*state));
		bzero(state, sizeof(*state));
		TAILQ_INIT(&state->subq);
		atomic_add_int(&pstate->refs, 1);
		state->parent = pstate;
		state->iocom = iocom;
		state->flags = DMSG_STATE_DYNAMIC;
		state->msgid = (uint64_t)(uintptr_t)state;
		state->txcmd = cmd & ~(DMSGF_CREATE | DMSGF_DELETE);
		state->rxcmd = DMSGF_REPLY;
		state->icmd = state->txcmd & DMSGF_BASECMDMASK;
		state->func = func;
		state->any.any = data;

		RB_INSERT(dmsg_state_tree, &iocom->statewr_tree, state);
		TAILQ_INSERT_TAIL(&pstate->subq, state, entry);
		state->flags |= DMSG_STATE_INSERTED;
	} else {
		/*
		 * Otherwise the message is transmitted over the existing
		 * open transaction.
		 */
		pstate = state->parent;
	}

	/* XXX SMP race for state */
	pthread_mutex_unlock(&iocom->mtx);
	hbytes = (cmd & DMSGF_SIZE) * DMSG_ALIGN;
	if (msg == NULL) {
		msg = malloc(offsetof(struct dmsg_msg, any.head) + hbytes + 4);
		bzero(msg, offsetof(struct dmsg_msg, any.head));
		*(int *)((char *)msg +
			 offsetof(struct dmsg_msg, any.head) + hbytes) =
				 0x71B2C3D4;
#if 0
		msg = malloc(sizeof(*msg));
		bzero(msg, sizeof(*msg));
#endif
	}

	/*
	 * [re]allocate the auxillary data buffer.  The caller knows that
	 * a size-aligned buffer will be allocated but we do not want to
	 * force the caller to zero any tail piece, so we do that ourself.
	 */
	if (msg->aux_size != aux_size) {
		if (msg->aux_data) {
			free(msg->aux_data);
			msg->aux_data = NULL;
			msg->aux_size = 0;
		}
		if (aux_size) {
			msg->aux_data = malloc(aligned_size);
			msg->aux_size = aux_size;
			if (aux_size != aligned_size) {
				bzero(msg->aux_data + aux_size,
				      aligned_size - aux_size);
			}
		}
	}

	/*
	 * Set REVTRANS if the transaction was remotely initiated
	 * Set REVCIRC if the circuit was remotely initiated
	 */
	if (state->flags & DMSG_STATE_OPPOSITE)
		cmd |= DMSGF_REVTRANS;
	if (pstate->flags & DMSG_STATE_OPPOSITE)
		cmd |= DMSGF_REVCIRC;

	/*
	 * Finish filling out the header.
	 */
	if (hbytes)
		bzero(&msg->any.head, hbytes);
	msg->hdr_size = hbytes;
	msg->any.head.magic = DMSG_HDR_MAGIC;
	msg->any.head.cmd = cmd;
	msg->any.head.aux_descr = 0;
	msg->any.head.aux_crc = 0;
	msg->any.head.msgid = state->msgid;
	msg->any.head.circuit = pstate->msgid;
	msg->state = state;

	return (msg);
}

/*
 * Free a message so it can be reused afresh.
 *
 * NOTE: aux_size can be 0 with a non-NULL aux_data.
 */
static
void
dmsg_msg_free_locked(dmsg_msg_t *msg)
{
	/*dmsg_iocom_t *iocom = msg->iocom;*/
#if 1
	int hbytes = (msg->any.head.cmd & DMSGF_SIZE) * DMSG_ALIGN;
	if (*(int *)((char *)msg +
		     offsetof(struct  dmsg_msg, any.head) + hbytes) !=
	     0x71B2C3D4) {
		fprintf(stderr, "MSGFREE FAILED CMD %08x\n", msg->any.head.cmd);
		assert(0);
	}
#endif
	msg->state = NULL;
	if (msg->aux_data) {
		free(msg->aux_data);
		msg->aux_data = NULL;
	}
	msg->aux_size = 0;
	free (msg);
#if 0
	if (msg->aux_data)
		TAILQ_INSERT_TAIL(&iocom->freeq_aux, msg, qentry);
	else
		TAILQ_INSERT_TAIL(&iocom->freeq, msg, qentry);
#endif
}

void
dmsg_msg_free(dmsg_msg_t *msg)
{
	dmsg_iocom_t *iocom = msg->state->iocom;

	pthread_mutex_lock(&iocom->mtx);
	dmsg_msg_free_locked(msg);
	pthread_mutex_unlock(&iocom->mtx);
}

/*
 * I/O core loop for an iocom.
 *
 * Thread localized, iocom->mtx not held.
 */
void
dmsg_iocom_core(dmsg_iocom_t *iocom)
{
	struct pollfd fds[3];
	char dummybuf[256];
	dmsg_msg_t *msg;
	int timeout;
	int count;
	int wi;	/* wakeup pipe */
	int si;	/* socket */
	int ai;	/* alt bulk path socket */

	while ((iocom->flags & DMSG_IOCOMF_EOF) == 0) {
		/*
		 * These iocom->flags are only manipulated within the
		 * context of the current thread.  However, modifications
		 * still require atomic ops.
		 */
		if ((iocom->flags & (DMSG_IOCOMF_RWORK |
				     DMSG_IOCOMF_WWORK |
				     DMSG_IOCOMF_PWORK |
				     DMSG_IOCOMF_SWORK |
				     DMSG_IOCOMF_ARWORK |
				     DMSG_IOCOMF_AWWORK)) == 0) {
			/*
			 * Only poll if no immediate work is pending.
			 * Otherwise we are just wasting our time calling
			 * poll.
			 */
			timeout = 5000;

			count = 0;
			wi = -1;
			si = -1;
			ai = -1;

			/*
			 * Always check the inter-thread pipe, e.g.
			 * for iocom->txmsgq work.
			 */
			wi = count++;
			fds[wi].fd = iocom->wakeupfds[0];
			fds[wi].events = POLLIN;
			fds[wi].revents = 0;

			/*
			 * Check the socket input/output direction as
			 * requested
			 */
			if (iocom->flags & (DMSG_IOCOMF_RREQ |
					    DMSG_IOCOMF_WREQ)) {
				si = count++;
				fds[si].fd = iocom->sock_fd;
				fds[si].events = 0;
				fds[si].revents = 0;

				if (iocom->flags & DMSG_IOCOMF_RREQ)
					fds[si].events |= POLLIN;
				if (iocom->flags & DMSG_IOCOMF_WREQ)
					fds[si].events |= POLLOUT;
			}

			/*
			 * Check the alternative fd for work.
			 */
			if (iocom->alt_fd >= 0) {
				ai = count++;
				fds[ai].fd = iocom->alt_fd;
				fds[ai].events = POLLIN;
				fds[ai].revents = 0;
			}
			poll(fds, count, timeout);

			if (wi >= 0 && (fds[wi].revents & POLLIN))
				atomic_set_int(&iocom->flags,
					       DMSG_IOCOMF_PWORK);
			if (si >= 0 && (fds[si].revents & POLLIN))
				atomic_set_int(&iocom->flags,
					       DMSG_IOCOMF_RWORK);
			if (si >= 0 && (fds[si].revents & POLLOUT))
				atomic_set_int(&iocom->flags,
					       DMSG_IOCOMF_WWORK);
			if (wi >= 0 && (fds[wi].revents & POLLOUT))
				atomic_set_int(&iocom->flags,
					       DMSG_IOCOMF_WWORK);
			if (ai >= 0 && (fds[ai].revents & POLLIN))
				atomic_set_int(&iocom->flags,
					       DMSG_IOCOMF_ARWORK);
		} else {
			/*
			 * Always check the pipe
			 */
			atomic_set_int(&iocom->flags, DMSG_IOCOMF_PWORK);
		}

		if (iocom->flags & DMSG_IOCOMF_SWORK) {
			atomic_clear_int(&iocom->flags, DMSG_IOCOMF_SWORK);
			iocom->signal_callback(iocom);
		}

		/*
		 * Pending message queues from other threads wake us up
		 * with a write to the wakeupfds[] pipe.  We have to clear
		 * the pipe with a dummy read.
		 */
		if (iocom->flags & DMSG_IOCOMF_PWORK) {
			atomic_clear_int(&iocom->flags, DMSG_IOCOMF_PWORK);
			read(iocom->wakeupfds[0], dummybuf, sizeof(dummybuf));
			atomic_set_int(&iocom->flags, DMSG_IOCOMF_RWORK);
			atomic_set_int(&iocom->flags, DMSG_IOCOMF_WWORK);
			if (TAILQ_FIRST(&iocom->txmsgq))
				dmsg_iocom_flush1(iocom);
		}

		/*
		 * Message write sequencing
		 */
		if (iocom->flags & DMSG_IOCOMF_WWORK)
			dmsg_iocom_flush1(iocom);

		/*
		 * Message read sequencing.  Run this after the write
		 * sequencing in case the write sequencing allowed another
		 * auto-DELETE to occur on the read side.
		 */
		if (iocom->flags & DMSG_IOCOMF_RWORK) {
			while ((iocom->flags & DMSG_IOCOMF_EOF) == 0 &&
			       (msg = dmsg_ioq_read(iocom)) != NULL) {
				if (DMsgDebugOpt) {
					fprintf(stderr, "receive %s\n",
						dmsg_msg_str(msg));
				}
				iocom->rcvmsg_callback(msg);
				dmsg_state_cleanuprx(iocom, msg);
			}
		}

		if (iocom->flags & DMSG_IOCOMF_ARWORK) {
			atomic_clear_int(&iocom->flags, DMSG_IOCOMF_ARWORK);
			iocom->altmsg_callback(iocom);
		}
	}
}

/*
 * Make sure there's enough room in the FIFO to hold the
 * needed data.
 *
 * Assume worst case encrypted form is 2x the size of the
 * plaintext equivalent.
 */
static
size_t
dmsg_ioq_makeroom(dmsg_ioq_t *ioq, size_t needed)
{
	size_t bytes;
	size_t nmax;

	bytes = ioq->fifo_cdx - ioq->fifo_beg;
	nmax = sizeof(ioq->buf) - ioq->fifo_end;
	if (bytes + nmax / 2 < needed) {
		if (bytes) {
			bcopy(ioq->buf + ioq->fifo_beg,
			      ioq->buf,
			      bytes);
		}
		ioq->fifo_cdx -= ioq->fifo_beg;
		ioq->fifo_beg = 0;
		if (ioq->fifo_cdn < ioq->fifo_end) {
			bcopy(ioq->buf + ioq->fifo_cdn,
			      ioq->buf + ioq->fifo_cdx,
			      ioq->fifo_end - ioq->fifo_cdn);
		}
		ioq->fifo_end -= ioq->fifo_cdn - ioq->fifo_cdx;
		ioq->fifo_cdn = ioq->fifo_cdx;
		nmax = sizeof(ioq->buf) - ioq->fifo_end;
	}
	return(nmax);
}

/*
 * Read the next ready message from the ioq, issuing I/O if needed.
 * Caller should retry on a read-event when NULL is returned.
 *
 * If an error occurs during reception a DMSG_LNK_ERROR msg will
 * be returned for each open transaction, then the ioq and iocom
 * will be errored out and a non-transactional DMSG_LNK_ERROR
 * msg will be returned as the final message.  The caller should not call
 * us again after the final message is returned.
 *
 * Thread localized, iocom->mtx not held.
 */
dmsg_msg_t *
dmsg_ioq_read(dmsg_iocom_t *iocom)
{
	dmsg_ioq_t *ioq = &iocom->ioq_rx;
	dmsg_msg_t *msg;
	dmsg_state_t *state;
	dmsg_hdr_t *head;
	ssize_t n;
	size_t bytes;
	size_t nmax;
	uint32_t aux_size;
	uint32_t xcrc32;
	int error;

again:
	/*
	 * If a message is already pending we can just remove and
	 * return it.  Message state has already been processed.
	 * (currently not implemented)
	 */
	if ((msg = TAILQ_FIRST(&ioq->msgq)) != NULL) {
		TAILQ_REMOVE(&ioq->msgq, msg, qentry);
		return (msg);
	}
	atomic_clear_int(&iocom->flags, DMSG_IOCOMF_RREQ | DMSG_IOCOMF_RWORK);

	/*
	 * If the stream is errored out we stop processing it.
	 */
	if (ioq->error)
		goto skip;

	/*
	 * Message read in-progress (msg is NULL at the moment).  We don't
	 * allocate a msg until we have its core header.
	 */
	nmax = sizeof(ioq->buf) - ioq->fifo_end;
	bytes = ioq->fifo_cdx - ioq->fifo_beg;		/* already decrypted */
	msg = ioq->msg;

	switch(ioq->state) {
	case DMSG_MSGQ_STATE_HEADER1:
		/*
		 * Load the primary header, fail on any non-trivial read
		 * error or on EOF.  Since the primary header is the same
		 * size is the message alignment it will never straddle
		 * the end of the buffer.
		 */
		nmax = dmsg_ioq_makeroom(ioq, sizeof(msg->any.head));
		if (bytes < sizeof(msg->any.head)) {
			n = read(iocom->sock_fd,
				 ioq->buf + ioq->fifo_end,
				 nmax);
			if (n <= 0) {
				if (n == 0) {
					ioq->error = DMSG_IOQ_ERROR_EOF;
					break;
				}
				if (errno != EINTR &&
				    errno != EINPROGRESS &&
				    errno != EAGAIN) {
					ioq->error = DMSG_IOQ_ERROR_SOCK;
					break;
				}
				n = 0;
				/* fall through */
			}
			ioq->fifo_end += (size_t)n;
			nmax -= (size_t)n;
		}

		/*
		 * Decrypt data received so far.  Data will be decrypted
		 * in-place but might create gaps in the FIFO.  Partial
		 * blocks are not immediately decrypted.
		 *
		 * WARNING!  The header might be in the wrong endian, we
		 *	     do not fix it up until we get the entire
		 *	     extended header.
		 */
		if (iocom->flags & DMSG_IOCOMF_CRYPTED) {
			dmsg_crypto_decrypt(iocom, ioq);
		} else {
			ioq->fifo_cdx = ioq->fifo_end;
			ioq->fifo_cdn = ioq->fifo_end;
		}
		bytes = ioq->fifo_cdx - ioq->fifo_beg;

		/*
		 * Insufficient data accumulated (msg is NULL, caller will
		 * retry on event).
		 */
		assert(msg == NULL);
		if (bytes < sizeof(msg->any.head))
			break;

		/*
		 * Check and fixup the core header.  Note that the icrc
		 * has to be calculated before any fixups, but the crc
		 * fields in the msg may have to be swapped like everything
		 * else.
		 */
		head = (void *)(ioq->buf + ioq->fifo_beg);
		if (head->magic != DMSG_HDR_MAGIC &&
		    head->magic != DMSG_HDR_MAGIC_REV) {
			fprintf(stderr, "%s: head->magic is bad %02x\n",
				iocom->label, head->magic);
			if (iocom->flags & DMSG_IOCOMF_CRYPTED)
				fprintf(stderr, "(on encrypted link)\n");
			ioq->error = DMSG_IOQ_ERROR_SYNC;
			break;
		}

		/*
		 * Calculate the full header size and aux data size
		 */
		if (head->magic == DMSG_HDR_MAGIC_REV) {
			ioq->hbytes = (bswap32(head->cmd) & DMSGF_SIZE) *
				      DMSG_ALIGN;
			aux_size = bswap32(head->aux_bytes);
		} else {
			ioq->hbytes = (head->cmd & DMSGF_SIZE) *
				      DMSG_ALIGN;
			aux_size = head->aux_bytes;
		}
		ioq->abytes = DMSG_DOALIGN(aux_size);
		ioq->unaligned_aux_size = aux_size;
		if (ioq->hbytes < sizeof(msg->any.head) ||
		    ioq->hbytes > sizeof(msg->any) ||
		    ioq->abytes > DMSG_AUX_MAX) {
			ioq->error = DMSG_IOQ_ERROR_FIELD;
			break;
		}

		/*
		 * Allocate the message, the next state will fill it in.
		 *
		 * NOTE: The aux_data buffer will be sized to an aligned
		 *	 value and the aligned remainder zero'd for
		 *	 convenience.
		 *
		 * NOTE: Supply dummy state and a degenerate cmd without
		 *	 CREATE set.  The message will temporarily be
		 *	 associated with state0 until later post-processing.
		 */
		msg = dmsg_msg_alloc(&iocom->state0, aux_size,
				     ioq->hbytes / DMSG_ALIGN,
				     NULL, NULL);
		ioq->msg = msg;

		/*
		 * Fall through to the next state.  Make sure that the
		 * extended header does not straddle the end of the buffer.
		 * We still want to issue larger reads into our buffer,
		 * book-keeping is easier if we don't bcopy() yet.
		 *
		 * Make sure there is enough room for bloated encrypt data.
		 */
		nmax = dmsg_ioq_makeroom(ioq, ioq->hbytes);
		ioq->state = DMSG_MSGQ_STATE_HEADER2;
		/* fall through */
	case DMSG_MSGQ_STATE_HEADER2:
		/*
		 * Fill out the extended header.
		 */
		assert(msg != NULL);
		if (bytes < ioq->hbytes) {
			n = read(iocom->sock_fd,
				 ioq->buf + ioq->fifo_end,
				 nmax);
			if (n <= 0) {
				if (n == 0) {
					ioq->error = DMSG_IOQ_ERROR_EOF;
					break;
				}
				if (errno != EINTR &&
				    errno != EINPROGRESS &&
				    errno != EAGAIN) {
					ioq->error = DMSG_IOQ_ERROR_SOCK;
					break;
				}
				n = 0;
				/* fall through */
			}
			ioq->fifo_end += (size_t)n;
			nmax -= (size_t)n;
		}

		if (iocom->flags & DMSG_IOCOMF_CRYPTED) {
			dmsg_crypto_decrypt(iocom, ioq);
		} else {
			ioq->fifo_cdx = ioq->fifo_end;
			ioq->fifo_cdn = ioq->fifo_end;
		}
		bytes = ioq->fifo_cdx - ioq->fifo_beg;

		/*
		 * Insufficient data accumulated (set msg NULL so caller will
		 * retry on event).
		 */
		if (bytes < ioq->hbytes) {
			msg = NULL;
			break;
		}

		/*
		 * Calculate the extended header, decrypt data received
		 * so far.  Handle endian-conversion for the entire extended
		 * header.
		 */
		head = (void *)(ioq->buf + ioq->fifo_beg);

		/*
		 * Check the CRC.
		 */
		if (head->magic == DMSG_HDR_MAGIC_REV)
			xcrc32 = bswap32(head->hdr_crc);
		else
			xcrc32 = head->hdr_crc;
		head->hdr_crc = 0;
		if (dmsg_icrc32(head, ioq->hbytes) != xcrc32) {
			ioq->error = DMSG_IOQ_ERROR_XCRC;
			fprintf(stderr, "BAD-XCRC(%08x,%08x) %s\n",
				xcrc32, dmsg_icrc32(head, ioq->hbytes),
				dmsg_msg_str(msg));
			assert(0);
			break;
		}
		head->hdr_crc = xcrc32;

		if (head->magic == DMSG_HDR_MAGIC_REV) {
			dmsg_bswap_head(head);
		}

		/*
		 * Copy the extended header into the msg and adjust the
		 * FIFO.
		 */
		bcopy(head, &msg->any, ioq->hbytes);

		/*
		 * We are either done or we fall-through.
		 */
		if (ioq->abytes == 0) {
			ioq->fifo_beg += ioq->hbytes;
			break;
		}

		/*
		 * Must adjust bytes (and the state) when falling through.
		 * nmax doesn't change.
		 */
		ioq->fifo_beg += ioq->hbytes;
		bytes -= ioq->hbytes;
		ioq->state = DMSG_MSGQ_STATE_AUXDATA1;
		/* fall through */
	case DMSG_MSGQ_STATE_AUXDATA1:
		/*
		 * Copy the partial or complete [decrypted] payload from
		 * remaining bytes in the FIFO in order to optimize the
		 * makeroom call in the AUXDATA2 state.  We have to
		 * fall-through either way so we can check the crc.
		 *
		 * msg->aux_size tracks our aux data.
		 *
		 * (Lets not complicate matters if the data is encrypted,
		 *  since the data in-stream is not the same size as the
		 *  data decrypted).
		 */
		if (bytes >= ioq->abytes) {
			bcopy(ioq->buf + ioq->fifo_beg, msg->aux_data,
			      ioq->abytes);
			msg->aux_size = ioq->abytes;
			ioq->fifo_beg += ioq->abytes;
			assert(ioq->fifo_beg <= ioq->fifo_cdx);
			assert(ioq->fifo_cdx <= ioq->fifo_cdn);
			bytes -= ioq->abytes;
		} else if (bytes) {
			bcopy(ioq->buf + ioq->fifo_beg, msg->aux_data,
			      bytes);
			msg->aux_size = bytes;
			ioq->fifo_beg += bytes;
			if (ioq->fifo_cdx < ioq->fifo_beg)
				ioq->fifo_cdx = ioq->fifo_beg;
			assert(ioq->fifo_beg <= ioq->fifo_cdx);
			assert(ioq->fifo_cdx <= ioq->fifo_cdn);
			bytes = 0;
		} else {
			msg->aux_size = 0;
		}
		ioq->state = DMSG_MSGQ_STATE_AUXDATA2;
		/* fall through */
	case DMSG_MSGQ_STATE_AUXDATA2:
		/*
		 * Make sure there is enough room for more data.
		 */
		assert(msg);
		nmax = dmsg_ioq_makeroom(ioq, ioq->abytes - msg->aux_size);

		/*
		 * Read and decrypt more of the payload.
		 */
		if (msg->aux_size < ioq->abytes) {
			assert(bytes == 0);
			n = read(iocom->sock_fd,
				 ioq->buf + ioq->fifo_end,
				 nmax);
			if (n <= 0) {
				if (n == 0) {
					ioq->error = DMSG_IOQ_ERROR_EOF;
					break;
				}
				if (errno != EINTR &&
				    errno != EINPROGRESS &&
				    errno != EAGAIN) {
					ioq->error = DMSG_IOQ_ERROR_SOCK;
					break;
				}
				n = 0;
				/* fall through */
			}
			ioq->fifo_end += (size_t)n;
			nmax -= (size_t)n;
		}

		if (iocom->flags & DMSG_IOCOMF_CRYPTED) {
			dmsg_crypto_decrypt(iocom, ioq);
		} else {
			ioq->fifo_cdx = ioq->fifo_end;
			ioq->fifo_cdn = ioq->fifo_end;
		}
		bytes = ioq->fifo_cdx - ioq->fifo_beg;

		if (bytes > ioq->abytes - msg->aux_size)
			bytes = ioq->abytes - msg->aux_size;

		if (bytes) {
			bcopy(ioq->buf + ioq->fifo_beg,
			      msg->aux_data + msg->aux_size,
			      bytes);
			msg->aux_size += bytes;
			ioq->fifo_beg += bytes;
		}

		/*
		 * Insufficient data accumulated (set msg NULL so caller will
		 * retry on event).
		 *
		 * Assert the auxillary data size is correct, then record the
		 * original unaligned size from the message header.
		 */
		if (msg->aux_size < ioq->abytes) {
			msg = NULL;
			break;
		}
		assert(msg->aux_size == ioq->abytes);
		msg->aux_size = ioq->unaligned_aux_size;

		/*
		 * Check aux_crc, then we are done.  Note that the crc
		 * is calculated over the aligned size, not the actual
		 * size.
		 */
		xcrc32 = dmsg_icrc32(msg->aux_data, ioq->abytes);
		if (xcrc32 != msg->any.head.aux_crc) {
			ioq->error = DMSG_IOQ_ERROR_ACRC;
			fprintf(stderr,
				"iocom: ACRC error %08x vs %08x "
				"msgid %016jx msgcmd %08x auxsize %d\n",
				xcrc32,
				msg->any.head.aux_crc,
				(intmax_t)msg->any.head.msgid,
				msg->any.head.cmd,
				msg->any.head.aux_bytes);
			break;
		}
		break;
	case DMSG_MSGQ_STATE_ERROR:
		/*
		 * Continued calls to drain recorded transactions (returning
		 * a LNK_ERROR for each one), before we return the final
		 * LNK_ERROR.
		 */
		assert(msg == NULL);
		break;
	default:
		/*
		 * We don't double-return errors, the caller should not
		 * have called us again after getting an error msg.
		 */
		assert(0);
		break;
	}

	/*
	 * Check the message sequence.  The iv[] should prevent any
	 * possibility of a replay but we add this check anyway.
	 */
	if (msg && ioq->error == 0) {
		if ((msg->any.head.salt & 255) != (ioq->seq & 255)) {
			ioq->error = DMSG_IOQ_ERROR_MSGSEQ;
		} else {
			++ioq->seq;
		}
	}

	/*
	 * Handle error, RREQ, or completion
	 *
	 * NOTE: nmax and bytes are invalid at this point, we don't bother
	 *	 to update them when breaking out.
	 */
	if (ioq->error) {
skip:
		fprintf(stderr, "IOQ ERROR %d\n", ioq->error);
		/*
		 * An unrecoverable error causes all active receive
		 * transactions to be terminated with a LNK_ERROR message.
		 *
		 * Once all active transactions are exhausted we set the
		 * iocom ERROR flag and return a non-transactional LNK_ERROR
		 * message, which should cause master processing loops to
		 * terminate.
		 */
		assert(ioq->msg == msg);
		if (msg) {
			dmsg_msg_free(msg);
			ioq->msg = NULL;
		}

		/*
		 * No more I/O read processing
		 */
		ioq->state = DMSG_MSGQ_STATE_ERROR;

		/*
		 * Simulate a remote LNK_ERROR DELETE msg for any open
		 * transactions, ending with a final non-transactional
		 * LNK_ERROR (that the session can detect) when no
		 * transactions remain.
		 *
		 * NOTE: Temporarily supply state0 and a degenerate cmd
		 *	 without CREATE set.  The real state will be
		 *	 assigned in the loop.
		 *
		 * NOTE: We are simulating a received message using our
		 *	 side of the state, so the DMSGF_REV* bits have
		 *	 to be reversed.
		 */
		msg = dmsg_msg_alloc(&iocom->state0, 0, DMSG_LNK_ERROR,
				     NULL, NULL);
		msg->any.head.error = ioq->error;

		pthread_mutex_lock(&iocom->mtx);
		dmsg_iocom_drain(iocom);

		if ((state = RB_ROOT(&iocom->staterd_tree)) != NULL) {
			/*
			 * Active remote transactions are still present.
			 * Simulate the other end sending us a DELETE.
			 */
			if (state->rxcmd & DMSGF_DELETE) {
				dmsg_msg_free(msg);
				fprintf(stderr,
					"iocom: ioq error(rd) %d sleeping "
					"state %p rxcmd %08x txcmd %08x "
					"func %p\n",
					ioq->error, state, state->rxcmd,
					state->txcmd, state->func);
				usleep(100000);	/* XXX */
				atomic_set_int(&iocom->flags,
					       DMSG_IOCOMF_RWORK);
				msg = NULL;
			} else {
				fprintf(stderr, "SIMULATE ERROR1\n");
				/*state->txcmd |= DMSGF_DELETE;*/
				msg->state = state;
				msg->any.head.msgid = state->msgid;
				msg->any.head.circuit = state->parent->msgid;
				msg->any.head.cmd |= DMSGF_ABORT |
						     DMSGF_DELETE;
				if ((state->parent->flags &
				     DMSG_STATE_OPPOSITE) == 0) {
					msg->any.head.cmd |= DMSGF_REVCIRC;
				}
			}
		} else if ((state = RB_ROOT(&iocom->statewr_tree)) != NULL) {
			/*
			 * Active local transactions are still present.
			 * Simulate the other end sending us a DELETE.
			 */
			if (state->rxcmd & DMSGF_DELETE) {
				dmsg_msg_free(msg);
				fprintf(stderr,
					"iocom: ioq error(wr) %d sleeping "
					"state %p rxcmd %08x txcmd %08x "
					"func %p\n",
					ioq->error, state, state->rxcmd,
					state->txcmd, state->func);
				usleep(100000);	/* XXX */
				atomic_set_int(&iocom->flags,
					       DMSG_IOCOMF_RWORK);
				msg = NULL;
			} else {
				fprintf(stderr, "SIMULATE ERROR1\n");
				msg->state = state;
				msg->any.head.msgid = state->msgid;
				msg->any.head.circuit = state->parent->msgid;
				msg->any.head.cmd |= DMSGF_ABORT |
						     DMSGF_DELETE |
						     DMSGF_REVTRANS |
						     DMSGF_REPLY;
				if ((state->parent->flags &
				     DMSG_STATE_OPPOSITE) == 0) {
					msg->any.head.cmd |= DMSGF_REVCIRC;
				}
				if ((state->rxcmd & DMSGF_CREATE) == 0)
					msg->any.head.cmd |= DMSGF_CREATE;
			}
		} else {
			/*
			 * No active local or remote transactions remain.
			 * Generate a final LNK_ERROR and flag EOF.
			 */
			atomic_set_int(&iocom->flags, DMSG_IOCOMF_EOF);
			fprintf(stderr, "EOF ON SOCKET %d\n", iocom->sock_fd);
		}
		pthread_mutex_unlock(&iocom->mtx);

		/*
		 * For the iocom error case we want to set RWORK to indicate
		 * that more messages might be pending.
		 *
		 * It is possible to return NULL when there is more work to
		 * do because each message has to be DELETEd in both
		 * directions before we continue on with the next (though
		 * this could be optimized).  The transmit direction will
		 * re-set RWORK.
		 */
		if (msg)
			atomic_set_int(&iocom->flags, DMSG_IOCOMF_RWORK);
	} else if (msg == NULL) {
		/*
		 * Insufficient data received to finish building the message,
		 * set RREQ and return NULL.
		 *
		 * Leave ioq->msg intact.
		 * Leave the FIFO intact.
		 */
		atomic_set_int(&iocom->flags, DMSG_IOCOMF_RREQ);
	} else {
		/*
		 * Continue processing msg.
		 *
		 * The fifo has already been advanced past the message.
		 * Trivially reset the FIFO indices if possible.
		 *
		 * clear the FIFO if it is now empty and set RREQ to wait
		 * for more from the socket.  If the FIFO is not empty set
		 * TWORK to bypass the poll so we loop immediately.
		 */
		if (ioq->fifo_beg == ioq->fifo_cdx &&
		    ioq->fifo_cdn == ioq->fifo_end) {
			atomic_set_int(&iocom->flags, DMSG_IOCOMF_RREQ);
			ioq->fifo_cdx = 0;
			ioq->fifo_cdn = 0;
			ioq->fifo_beg = 0;
			ioq->fifo_end = 0;
		} else {
			atomic_set_int(&iocom->flags, DMSG_IOCOMF_RWORK);
		}
		ioq->state = DMSG_MSGQ_STATE_HEADER1;
		ioq->msg = NULL;

		/*
		 * Handle message routing.  Validates non-zero sources
		 * and routes message.  Error will be 0 if the message is
		 * destined for us.
		 *
		 * State processing only occurs for messages destined for us.
		 */
		if (DMsgDebugOpt >= 5) {
			fprintf(stderr,
				"rxmsg cmd=%08x msgid=%016jx circ=%016jx\n",
				msg->any.head.cmd,
				(intmax_t)msg->any.head.msgid,
				(intmax_t)msg->any.head.circuit);
		}
		error = dmsg_state_msgrx(msg);

		if (error) {
			/*
			 * Abort-after-closure, throw message away and
			 * start reading another.
			 */
			if (error == DMSG_IOQ_ERROR_EALREADY) {
				dmsg_msg_free(msg);
				goto again;
			}

			/*
			 * Process real error and throw away message.
			 */
			ioq->error = error;
			goto skip;
		}
		/* no error, not routed.  Fall through and return msg */
	}
	return (msg);
}

/*
 * Calculate the header and data crc's and write a low-level message to
 * the connection.  If aux_crc is non-zero the aux_data crc is already
 * assumed to have been set.
 *
 * A non-NULL msg is added to the queue but not necessarily flushed.
 * Calling this function with msg == NULL will get a flush going.
 *
 * (called from iocom_core only)
 */
void
dmsg_iocom_flush1(dmsg_iocom_t *iocom)
{
	dmsg_ioq_t *ioq = &iocom->ioq_tx;
	dmsg_msg_t *msg;
	uint32_t xcrc32;
	size_t hbytes;
	size_t abytes;
	dmsg_msg_queue_t tmpq;

	atomic_clear_int(&iocom->flags, DMSG_IOCOMF_WREQ | DMSG_IOCOMF_WWORK);
	TAILQ_INIT(&tmpq);
	pthread_mutex_lock(&iocom->mtx);
	while ((msg = TAILQ_FIRST(&iocom->txmsgq)) != NULL) {
		TAILQ_REMOVE(&iocom->txmsgq, msg, qentry);
		TAILQ_INSERT_TAIL(&tmpq, msg, qentry);
	}
	pthread_mutex_unlock(&iocom->mtx);

	while ((msg = TAILQ_FIRST(&tmpq)) != NULL) {
		/*
		 * Process terminal connection errors.
		 */
		TAILQ_REMOVE(&tmpq, msg, qentry);
		if (ioq->error) {
			TAILQ_INSERT_TAIL(&ioq->msgq, msg, qentry);
			++ioq->msgcount;
			continue;
		}

		/*
		 * Finish populating the msg fields.  The salt ensures that
		 * the iv[] array is ridiculously randomized and we also
		 * re-seed our PRNG every 32768 messages just to be sure.
		 */
		msg->any.head.magic = DMSG_HDR_MAGIC;
		msg->any.head.salt = (random() << 8) | (ioq->seq & 255);
		++ioq->seq;
		if ((ioq->seq & 32767) == 0)
			srandomdev();

		/*
		 * Calculate aux_crc if 0, then calculate hdr_crc.
		 */
		if (msg->aux_size && msg->any.head.aux_crc == 0) {
			abytes = DMSG_DOALIGN(msg->aux_size);
			xcrc32 = dmsg_icrc32(msg->aux_data, abytes);
			msg->any.head.aux_crc = xcrc32;
		}
		msg->any.head.aux_bytes = msg->aux_size;

		hbytes = (msg->any.head.cmd & DMSGF_SIZE) *
			 DMSG_ALIGN;
		msg->any.head.hdr_crc = 0;
		msg->any.head.hdr_crc = dmsg_icrc32(&msg->any.head, hbytes);

		/*
		 * Enqueue the message (the flush codes handles stream
		 * encryption).
		 */
		TAILQ_INSERT_TAIL(&ioq->msgq, msg, qentry);
		++ioq->msgcount;
	}
	dmsg_iocom_flush2(iocom);
}

/*
 * Thread localized, iocom->mtx not held by caller.
 *
 * (called from iocom_core via iocom_flush1 only)
 */
void
dmsg_iocom_flush2(dmsg_iocom_t *iocom)
{
	dmsg_ioq_t *ioq = &iocom->ioq_tx;
	dmsg_msg_t *msg;
	ssize_t n;
	struct iovec iov[DMSG_IOQ_MAXIOVEC];
	size_t nact;
	size_t hbytes;
	size_t abytes;
	size_t hoff;
	size_t aoff;
	int iovcnt;

	if (ioq->error) {
		dmsg_iocom_drain(iocom);
		return;
	}

	/*
	 * Pump messages out the connection by building an iovec.
	 *
	 * ioq->hbytes/ioq->abytes tracks how much of the first message
	 * in the queue has been successfully written out, so we can
	 * resume writing.
	 */
	iovcnt = 0;
	nact = 0;
	hoff = ioq->hbytes;
	aoff = ioq->abytes;

	TAILQ_FOREACH(msg, &ioq->msgq, qentry) {
		hbytes = (msg->any.head.cmd & DMSGF_SIZE) *
			 DMSG_ALIGN;
		abytes = DMSG_DOALIGN(msg->aux_size);
		assert(hoff <= hbytes && aoff <= abytes);

		if (hoff < hbytes) {
			iov[iovcnt].iov_base = (char *)&msg->any.head + hoff;
			iov[iovcnt].iov_len = hbytes - hoff;
			nact += hbytes - hoff;
			++iovcnt;
			if (iovcnt == DMSG_IOQ_MAXIOVEC)
				break;
		}
		if (aoff < abytes) {
			assert(msg->aux_data != NULL);
			iov[iovcnt].iov_base = (char *)msg->aux_data + aoff;
			iov[iovcnt].iov_len = abytes - aoff;
			nact += abytes - aoff;
			++iovcnt;
			if (iovcnt == DMSG_IOQ_MAXIOVEC)
				break;
		}
		hoff = 0;
		aoff = 0;
	}
	if (iovcnt == 0)
		return;

	/*
	 * Encrypt and write the data.  The crypto code will move the
	 * data into the fifo and adjust the iov as necessary.  If
	 * encryption is disabled the iov is left alone.
	 *
	 * May return a smaller iov (thus a smaller n), with aggregated
	 * chunks.  May reduce nmax to what fits in the FIFO.
	 *
	 * This function sets nact to the number of original bytes now
	 * encrypted, adding to the FIFO some number of bytes that might
	 * be greater depending on the crypto mechanic.  iov[] is adjusted
	 * to point at the FIFO if necessary.
	 *
	 * NOTE: The return value from the writev() is the post-encrypted
	 *	 byte count, not the plaintext count.
	 */
	if (iocom->flags & DMSG_IOCOMF_CRYPTED) {
		/*
		 * Make sure the FIFO has a reasonable amount of space
		 * left (if not completely full).
		 *
		 * In this situation we are staging the encrypted message
		 * data in the FIFO.  (nact) represents how much plaintext
		 * has been staged, (n) represents how much encrypted data
		 * has been flushed.  The two are independent of each other.
		 */
		if (ioq->fifo_beg > sizeof(ioq->buf) / 2 &&
		    sizeof(ioq->buf) - ioq->fifo_end < DMSG_ALIGN * 2) {
			bcopy(ioq->buf + ioq->fifo_beg, ioq->buf,
			      ioq->fifo_end - ioq->fifo_beg);
			ioq->fifo_cdx -= ioq->fifo_beg;
			ioq->fifo_cdn -= ioq->fifo_beg;
			ioq->fifo_end -= ioq->fifo_beg;
			ioq->fifo_beg = 0;
		}

		iovcnt = dmsg_crypto_encrypt(iocom, ioq, iov, iovcnt, &nact);
		n = writev(iocom->sock_fd, iov, iovcnt);
		if (n > 0) {
			ioq->fifo_beg += n;
			ioq->fifo_cdn += n;
			ioq->fifo_cdx += n;
			if (ioq->fifo_beg == ioq->fifo_end) {
				ioq->fifo_beg = 0;
				ioq->fifo_cdn = 0;
				ioq->fifo_cdx = 0;
				ioq->fifo_end = 0;
			}
		}
		/*
		 * We don't mess with the nact returned by the crypto_encrypt
		 * call, which represents the filling of the FIFO.  (n) tells
		 * us how much we were able to write from the FIFO.  The two
		 * are different beasts when encrypting.
		 */
	} else {
		/*
		 * In this situation we are not staging the messages to the
		 * FIFO but instead writing them directly from the msg
		 * structure(s), so (nact) is basically (n).
		 */
		n = writev(iocom->sock_fd, iov, iovcnt);
		if (n > 0)
			nact = n;
		else
			nact = 0;
	}

	/*
	 * Clean out the transmit queue based on what we successfully
	 * sent (nact is the plaintext count).  ioq->hbytes/abytes
	 * represents the portion of the first message previously sent.
	 */
	while ((msg = TAILQ_FIRST(&ioq->msgq)) != NULL) {
		hbytes = (msg->any.head.cmd & DMSGF_SIZE) *
			 DMSG_ALIGN;
		abytes = DMSG_DOALIGN(msg->aux_size);

		if ((size_t)nact < hbytes - ioq->hbytes) {
			ioq->hbytes += nact;
			nact = 0;
			break;
		}
		nact -= hbytes - ioq->hbytes;
		ioq->hbytes = hbytes;
		if ((size_t)nact < abytes - ioq->abytes) {
			ioq->abytes += nact;
			nact = 0;
			break;
		}
		nact -= abytes - ioq->abytes;
		/* ioq->abytes = abytes; optimized out */

#if 0
		fprintf(stderr,
			"txmsg cmd=%08x msgid=%016jx circ=%016jx\n",
			msg->any.head.cmd,
			(intmax_t)msg->any.head.msgid,
			(intmax_t)msg->any.head.circuit);
#endif

		TAILQ_REMOVE(&ioq->msgq, msg, qentry);
		--ioq->msgcount;
		ioq->hbytes = 0;
		ioq->abytes = 0;

		dmsg_state_cleanuptx(iocom, msg);
	}
	assert(nact == 0);

	/*
	 * Process the return value from the write w/regards to blocking.
	 */
	if (n < 0) {
		if (errno != EINTR &&
		    errno != EINPROGRESS &&
		    errno != EAGAIN) {
			/*
			 * Fatal write error
			 */
			ioq->error = DMSG_IOQ_ERROR_SOCK;
			dmsg_iocom_drain(iocom);
		} else {
			/*
			 * Wait for socket buffer space
			 */
			atomic_set_int(&iocom->flags, DMSG_IOCOMF_WREQ);
		}
	} else {
		atomic_set_int(&iocom->flags, DMSG_IOCOMF_WREQ);
	}
	if (ioq->error) {
		dmsg_iocom_drain(iocom);
	}
}

/*
 * Kill pending msgs on ioq_tx and adjust the flags such that no more
 * write events will occur.  We don't kill read msgs because we want
 * the caller to pull off our contrived terminal error msg to detect
 * the connection failure.
 *
 * Localized to iocom_core thread, iocom->mtx not held by caller.
 */
void
dmsg_iocom_drain(dmsg_iocom_t *iocom)
{
	dmsg_ioq_t *ioq = &iocom->ioq_tx;
	dmsg_msg_t *msg;

	atomic_clear_int(&iocom->flags, DMSG_IOCOMF_WREQ | DMSG_IOCOMF_WWORK);
	ioq->hbytes = 0;
	ioq->abytes = 0;

	while ((msg = TAILQ_FIRST(&ioq->msgq)) != NULL) {
		TAILQ_REMOVE(&ioq->msgq, msg, qentry);
		--ioq->msgcount;
		dmsg_state_cleanuptx(iocom, msg);
	}
}

/*
 * Write a message to an iocom, with additional state processing.
 */
void
dmsg_msg_write(dmsg_msg_t *msg)
{
	dmsg_iocom_t *iocom = msg->state->iocom;
	dmsg_state_t *state;
	char dummy;

	/*
	 * Handle state processing, create state if necessary.
	 */
	pthread_mutex_lock(&iocom->mtx);
	state = msg->state;

	if ((state->flags & DMSG_STATE_ROOT) == 0) {
		/*
		 * Existing transaction (could be reply).  It is also
		 * possible for this to be the first reply (CREATE is set),
		 * in which case we populate state->txcmd.
		 *
		 * state->txcmd is adjusted to hold the final message cmd,
		 * and we also be sure to set the CREATE bit here.  We did
		 * not set it in dmsg_msg_alloc() because that would have
		 * not been serialized (state could have gotten ripped out
		 * from under the message prior to it being transmitted).
		 */
		if ((msg->any.head.cmd & (DMSGF_CREATE | DMSGF_REPLY)) ==
		    DMSGF_CREATE) {
			state->txcmd = msg->any.head.cmd & ~DMSGF_DELETE;
			state->icmd = state->txcmd & DMSGF_BASECMDMASK;
		}
		msg->any.head.msgid = state->msgid;

		if (msg->any.head.cmd & DMSGF_CREATE) {
			state->txcmd = msg->any.head.cmd & ~DMSGF_DELETE;
		}
	}

#if 0
	fprintf(stderr,
		"MSGWRITE %016jx %08x\n",
		msg->any.head.msgid, msg->any.head.cmd);
#endif

	/*
	 * Queue it for output, wake up the I/O pthread.  Note that the
	 * I/O thread is responsible for generating the CRCs and encryption.
	 */
	TAILQ_INSERT_TAIL(&iocom->txmsgq, msg, qentry);
	dummy = 0;
	write(iocom->wakeupfds[1], &dummy, 1);	/* XXX optimize me */
	pthread_mutex_unlock(&iocom->mtx);
}

/*
 * This is a shortcut to formulate a reply to msg with a simple error code,
 * It can reply to and terminate a transaction, or it can reply to a one-way
 * messages.  A DMSG_LNK_ERROR command code is utilized to encode
 * the error code (which can be 0).  Not all transactions are terminated
 * with DMSG_LNK_ERROR status (the low level only cares about the
 * MSGF_DELETE flag), but most are.
 *
 * Replies to one-way messages are a bit of an oxymoron but the feature
 * is used by the debug (DBG) protocol.
 *
 * The reply contains no extended data.
 */
void
dmsg_msg_reply(dmsg_msg_t *msg, uint32_t error)
{
	dmsg_state_t *state = msg->state;
	dmsg_msg_t *nmsg;
	uint32_t cmd;

	/*
	 * Reply with a simple error code and terminate the transaction.
	 */
	cmd = DMSG_LNK_ERROR;

	/*
	 * Check if our direction has even been initiated yet, set CREATE.
	 *
	 * Check what direction this is (command or reply direction).  Note
	 * that txcmd might not have been initiated yet.
	 *
	 * If our direction has already been closed we just return without
	 * doing anything.
	 */
	if ((state->flags & DMSG_STATE_ROOT) == 0) {
		if (state->txcmd & DMSGF_DELETE)
			return;
		if (state->txcmd & DMSGF_REPLY)
			cmd |= DMSGF_REPLY;
		cmd |= DMSGF_DELETE;
	} else {
		if ((msg->any.head.cmd & DMSGF_REPLY) == 0)
			cmd |= DMSGF_REPLY;
	}

	/*
	 * Allocate the message and associate it with the existing state.
	 * We cannot pass DMSGF_CREATE to msg_alloc() because that may
	 * allocate new state.  We have our state already.
	 */
	nmsg = dmsg_msg_alloc(state, 0, cmd, NULL, NULL);
	if ((state->flags & DMSG_STATE_ROOT) == 0) {
		if ((state->txcmd & DMSGF_CREATE) == 0)
			nmsg->any.head.cmd |= DMSGF_CREATE;
	}
	nmsg->any.head.error = error;

	dmsg_msg_write(nmsg);
}

/*
 * Similar to dmsg_msg_reply() but leave the transaction open.  That is,
 * we are generating a streaming reply or an intermediate acknowledgement
 * of some sort as part of the higher level protocol, with more to come
 * later.
 */
void
dmsg_msg_result(dmsg_msg_t *msg, uint32_t error)
{
	dmsg_state_t *state = msg->state;
	dmsg_msg_t *nmsg;
	uint32_t cmd;


	/*
	 * Reply with a simple error code and terminate the transaction.
	 */
	cmd = DMSG_LNK_ERROR;

	/*
	 * Check if our direction has even been initiated yet, set CREATE.
	 *
	 * Check what direction this is (command or reply direction).  Note
	 * that txcmd might not have been initiated yet.
	 *
	 * If our direction has already been closed we just return without
	 * doing anything.
	 */
	if ((state->flags & DMSG_STATE_ROOT) == 0) {
		if (state->txcmd & DMSGF_DELETE)
			return;
		if (state->txcmd & DMSGF_REPLY)
			cmd |= DMSGF_REPLY;
		/* continuing transaction, do not set MSGF_DELETE */
	} else {
		if ((msg->any.head.cmd & DMSGF_REPLY) == 0)
			cmd |= DMSGF_REPLY;
	}
	nmsg = dmsg_msg_alloc(state, 0, cmd, NULL, NULL);
	if ((state->flags & DMSG_STATE_ROOT) == 0) {
		if ((state->txcmd & DMSGF_CREATE) == 0)
			nmsg->any.head.cmd |= DMSGF_CREATE;
	}
	nmsg->any.head.error = error;

	dmsg_msg_write(nmsg);
}

/*
 * Terminate a transaction given a state structure by issuing a DELETE.
 * (the state structure must not be &iocom->state0)
 */
void
dmsg_state_reply(dmsg_state_t *state, uint32_t error)
{
	dmsg_msg_t *nmsg;
	uint32_t cmd = DMSG_LNK_ERROR | DMSGF_DELETE;

	/*
	 * Nothing to do if we already transmitted a delete
	 */
	if (state->txcmd & DMSGF_DELETE)
		return;

	/*
	 * Set REPLY if the other end initiated the command.  Otherwise
	 * we are the command direction.
	 */
	if (state->txcmd & DMSGF_REPLY)
		cmd |= DMSGF_REPLY;

	nmsg = dmsg_msg_alloc(state, 0, cmd, NULL, NULL);
	if ((state->flags & DMSG_STATE_ROOT) == 0) {
		if ((state->txcmd & DMSGF_CREATE) == 0)
			nmsg->any.head.cmd |= DMSGF_CREATE;
	}
	nmsg->any.head.error = error;
	dmsg_msg_write(nmsg);
}

/*
 * Terminate a transaction given a state structure by issuing a DELETE.
 * (the state structure must not be &iocom->state0)
 */
void
dmsg_state_result(dmsg_state_t *state, uint32_t error)
{
	dmsg_msg_t *nmsg;
	uint32_t cmd = DMSG_LNK_ERROR;

	/*
	 * Nothing to do if we already transmitted a delete
	 */
	if (state->txcmd & DMSGF_DELETE)
		return;

	/*
	 * Set REPLY if the other end initiated the command.  Otherwise
	 * we are the command direction.
	 */
	if (state->txcmd & DMSGF_REPLY)
		cmd |= DMSGF_REPLY;

	nmsg = dmsg_msg_alloc(state, 0, cmd, NULL, NULL);
	if ((state->flags & DMSG_STATE_ROOT) == 0) {
		if ((state->txcmd & DMSGF_CREATE) == 0)
			nmsg->any.head.cmd |= DMSGF_CREATE;
	}
	nmsg->any.head.error = error;
	dmsg_msg_write(nmsg);
}

/************************************************************************
 *			TRANSACTION STATE HANDLING			*
 ************************************************************************
 *
 */

/*
 * Process state tracking for a message after reception, prior to execution.
 * Possibly route the message (consuming it).
 *
 * Called with msglk held and the msg dequeued.
 *
 * All messages are called with dummy state and return actual state.
 * (One-off messages often just return the same dummy state).
 *
 * May request that caller discard the message by setting *discardp to 1.
 * The returned state is not used in this case and is allowed to be NULL.
 *
 * --
 *
 * These routines handle persistent and command/reply message state via the
 * CREATE and DELETE flags.  The first message in a command or reply sequence
 * sets CREATE, the last message in a command or reply sequence sets DELETE.
 *
 * There can be any number of intermediate messages belonging to the same
 * sequence sent inbetween the CREATE message and the DELETE message,
 * which set neither flag.  This represents a streaming command or reply.
 *
 * Any command message received with CREATE set expects a reply sequence to
 * be returned.  Reply sequences work the same as command sequences except the
 * REPLY bit is also sent.  Both the command side and reply side can
 * degenerate into a single message with both CREATE and DELETE set.  Note
 * that one side can be streaming and the other side not, or neither, or both.
 *
 * The msgid is unique for the initiator.  That is, two sides sending a new
 * message can use the same msgid without colliding.
 *
 * --
 *
 * ABORT sequences work by setting the ABORT flag along with normal message
 * state.  However, ABORTs can also be sent on half-closed messages, that is
 * even if the command or reply side has already sent a DELETE, as long as
 * the message has not been fully closed it can still send an ABORT+DELETE
 * to terminate the half-closed message state.
 *
 * Since ABORT+DELETEs can race we silently discard ABORT's for message
 * state which has already been fully closed.  REPLY+ABORT+DELETEs can
 * also race, and in this situation the other side might have already
 * initiated a new unrelated command with the same message id.  Since
 * the abort has not set the CREATE flag the situation can be detected
 * and the message will also be discarded.
 *
 * Non-blocking requests can be initiated with ABORT+CREATE[+DELETE].
 * The ABORT request is essentially integrated into the command instead
 * of being sent later on.  In this situation the command implementation
 * detects that CREATE and ABORT are both set (vs ABORT alone) and can
 * special-case non-blocking operation for the command.
 *
 * NOTE!  Messages with ABORT set without CREATE or DELETE are considered
 *	  to be mid-stream aborts for command/reply sequences.  ABORTs on
 *	  one-way messages are not supported.
 *
 * NOTE!  If a command sequence does not support aborts the ABORT flag is
 *	  simply ignored.
 *
 * --
 *
 * One-off messages (no reply expected) are sent without an established
 * transaction.  CREATE and DELETE are left clear and the msgid is usually 0.
 * For one-off messages sent over circuits msgid generally MUST be 0.
 *
 * One-off messages cannot be aborted and typically aren't processed
 * by these routines.  Order is still guaranteed for messages sent over
 * the same circuit.  The REPLY bit can be used to distinguish whether
 * a one-off message is a command or reply.  For example, one-off replies
 * will typically just contain status updates.
 */
static int
dmsg_state_msgrx(dmsg_msg_t *msg)
{
	dmsg_iocom_t *iocom = msg->state->iocom;
	dmsg_state_t *state;
	dmsg_state_t *pstate;
	dmsg_state_t sdummy;
	int error;

	pthread_mutex_lock(&iocom->mtx);

	/*
	 * Lookup the circuit (pstate).  The circuit will be an open
	 * transaction.  The REVCIRC bit in the message tells us which side
	 * initiated it.
	 */
	if (msg->any.head.circuit) {
		sdummy.msgid = msg->any.head.circuit;

		if (msg->any.head.cmd & DMSGF_REVCIRC) {
			pstate = RB_FIND(dmsg_state_tree,
					 &iocom->statewr_tree,
					 &sdummy);
		} else {
			pstate = RB_FIND(dmsg_state_tree,
					 &iocom->staterd_tree,
					 &sdummy);
		}
		if (pstate == NULL) {
			fprintf(stderr,
				"missing parent in stacked trans %s\n",
				dmsg_msg_str(msg));
			error = DMSG_IOQ_ERROR_TRANS;
			pthread_mutex_unlock(&iocom->mtx);
			assert(0);
		}
	} else {
		pstate = &iocom->state0;
	}

	/*
	 * Lookup the msgid.
	 *
	 * If received msg is a command state is on staterd_tree.
	 * If received msg is a reply state is on statewr_tree.
	 * Otherwise there is no state (retain &iocom->state0)
	 */
	sdummy.msgid = msg->any.head.msgid;
	if (msg->any.head.cmd & DMSGF_REVTRANS)
		state = RB_FIND(dmsg_state_tree, &iocom->statewr_tree, &sdummy);
	else
		state = RB_FIND(dmsg_state_tree, &iocom->staterd_tree, &sdummy);

	if (state) {
		/*
		 * Message over an existing transaction (CREATE should not
		 * be set).
		 */
		msg->state = state;
		assert(pstate == state->parent);
	} else {
		/*
		 * Either a new transaction (if CREATE set) or a one-off.
		 */
		state = pstate;
	}

	pthread_mutex_unlock(&iocom->mtx);

	/*
	 * Switch on CREATE, DELETE, REPLY, and also handle ABORT from
	 * inside the case statements.
	 *
	 * Construct new state as necessary.
	 */
	switch(msg->any.head.cmd & (DMSGF_CREATE | DMSGF_DELETE |
				    DMSGF_REPLY)) {
	case DMSGF_CREATE:
	case DMSGF_CREATE | DMSGF_DELETE:
		/*
		 * Create new sub-transaction under pstate.
		 * (any DELETE is handled in post-processing of msg).
		 *
		 * (During routing the msgid was made unique for this
		 * direction over the comlink, so our RB trees can be
		 * iocom-based instead of state-based).
		 */
		if (state != pstate) {
			fprintf(stderr,
				"duplicate transaction %s\n",
				dmsg_msg_str(msg));
			error = DMSG_IOQ_ERROR_TRANS;
			assert(0);
			break;
		}

		/*
		 * Allocate the new state.
		 */
		state = malloc(sizeof(*state));
		bzero(state, sizeof(*state));
		TAILQ_INIT(&state->subq);
		atomic_add_int(&pstate->refs, 1);
		state->parent = pstate;
		state->iocom = iocom;
		state->flags = DMSG_STATE_DYNAMIC |
			       DMSG_STATE_OPPOSITE;
		state->msgid = msg->any.head.msgid;
		state->txcmd = DMSGF_REPLY;
		state->rxcmd = msg->any.head.cmd & ~DMSGF_DELETE;
		state->icmd = state->rxcmd & DMSGF_BASECMDMASK;
		msg->state = state;
		pthread_mutex_lock(&iocom->mtx);
		RB_INSERT(dmsg_state_tree, &iocom->staterd_tree, state);
		TAILQ_INSERT_TAIL(&pstate->subq, state, entry);
		state->flags |= DMSG_STATE_INSERTED;

		/*
		 * If the parent is a relay set up the state handler to
		 * automatically route the message.  Local processing will
		 * not occur if set.
		 *
		 * (state relays are seeded by SPAN processing)
		 */
		if (pstate->relay)
			state->func = dmsg_state_relay;
		pthread_mutex_unlock(&iocom->mtx);
		error = 0;

		if (DMsgDebugOpt) {
			fprintf(stderr,
				"create state %p id=%08x on iocom staterd %p\n",
				state, (uint32_t)state->msgid, iocom);
		}
		break;
	case DMSGF_DELETE:
		/*
		 * Persistent state is expected but might not exist if an
		 * ABORT+DELETE races the close.
		 *
		 * (any DELETE is handled in post-processing of msg).
		 */
		if (state == pstate) {
			if (msg->any.head.cmd & DMSGF_ABORT) {
				error = DMSG_IOQ_ERROR_EALREADY;
			} else {
				fprintf(stderr, "missing-state %s\n",
					dmsg_msg_str(msg));
				error = DMSG_IOQ_ERROR_TRANS;
				assert(0);
			}
			break;
		}

		/*
		 * Handle another ABORT+DELETE case if the msgid has already
		 * been reused.
		 */
		if ((state->rxcmd & DMSGF_CREATE) == 0) {
			if (msg->any.head.cmd & DMSGF_ABORT) {
				error = DMSG_IOQ_ERROR_EALREADY;
			} else {
				fprintf(stderr, "reused-state %s\n",
					dmsg_msg_str(msg));
				error = DMSG_IOQ_ERROR_TRANS;
				assert(0);
			}
			break;
		}
		error = 0;
		break;
	default:
		/*
		 * Check for mid-stream ABORT command received, otherwise
		 * allow.
		 */
		if (msg->any.head.cmd & DMSGF_ABORT) {
			if ((state == pstate) ||
			    (state->rxcmd & DMSGF_CREATE) == 0) {
				error = DMSG_IOQ_ERROR_EALREADY;
				break;
			}
		}
		error = 0;
		break;
	case DMSGF_REPLY | DMSGF_CREATE:
	case DMSGF_REPLY | DMSGF_CREATE | DMSGF_DELETE:
		/*
		 * When receiving a reply with CREATE set the original
		 * persistent state message should already exist.
		 */
		if (state == pstate) {
			fprintf(stderr, "no-state(r) %s\n",
				dmsg_msg_str(msg));
			error = DMSG_IOQ_ERROR_TRANS;
			assert(0);
			break;
		}
		assert(((state->rxcmd ^ msg->any.head.cmd) & DMSGF_REPLY) == 0);
		state->rxcmd = msg->any.head.cmd & ~DMSGF_DELETE;
		error = 0;
		break;
	case DMSGF_REPLY | DMSGF_DELETE:
		/*
		 * Received REPLY+ABORT+DELETE in case where msgid has
		 * already been fully closed, ignore the message.
		 */
		if (state == pstate) {
			if (msg->any.head.cmd & DMSGF_ABORT) {
				error = DMSG_IOQ_ERROR_EALREADY;
			} else {
				fprintf(stderr, "no-state(r,d) %s\n",
					dmsg_msg_str(msg));
				error = DMSG_IOQ_ERROR_TRANS;
				assert(0);
			}
			break;
		}

		/*
		 * Received REPLY+ABORT+DELETE in case where msgid has
		 * already been reused for an unrelated message,
		 * ignore the message.
		 */
		if ((state->rxcmd & DMSGF_CREATE) == 0) {
			if (msg->any.head.cmd & DMSGF_ABORT) {
				error = DMSG_IOQ_ERROR_EALREADY;
			} else {
				fprintf(stderr, "reused-state(r,d) %s\n",
					dmsg_msg_str(msg));
				error = DMSG_IOQ_ERROR_TRANS;
				assert(0);
			}
			break;
		}
		error = 0;
		break;
	case DMSGF_REPLY:
		/*
		 * Check for mid-stream ABORT reply received to sent command.
		 */
		if (msg->any.head.cmd & DMSGF_ABORT) {
			if (state == pstate ||
			    (state->rxcmd & DMSGF_CREATE) == 0) {
				error = DMSG_IOQ_ERROR_EALREADY;
				break;
			}
		}
		error = 0;
		break;
	}

	/*
	 * Calculate the easy-switch() transactional command.  Represents
	 * the outer-transaction command for any transaction-create or
	 * transaction-delete, and the inner message command for any
	 * non-transaction or inside-transaction command.  tcmd will be
	 * set to 0 for any messaging error condition.
	 *
	 * The two can be told apart because outer-transaction commands
	 * always have a DMSGF_CREATE and/or DMSGF_DELETE flag.
	 */
	if (msg->any.head.cmd & (DMSGF_CREATE | DMSGF_DELETE)) {
		if ((state->flags & DMSG_STATE_ROOT) == 0) {
			msg->tcmd = (msg->state->icmd & DMSGF_BASECMDMASK) |
				    (msg->any.head.cmd & (DMSGF_CREATE |
							  DMSGF_DELETE |
							  DMSGF_REPLY));
		} else {
			msg->tcmd = 0;
		}
	} else {
		msg->tcmd = msg->any.head.cmd & DMSGF_CMDSWMASK;
	}
	return (error);
}

/*
 * Route the message and handle pair-state processing.
 */
void
dmsg_state_relay(dmsg_msg_t *lmsg)
{
	dmsg_state_t *lpstate;
	dmsg_state_t *rpstate;
	dmsg_state_t *lstate;
	dmsg_state_t *rstate;
	dmsg_msg_t *rmsg;

	if ((lmsg->any.head.cmd & (DMSGF_CREATE | DMSGF_REPLY)) ==
	    DMSGF_CREATE) {
		/*
		 * New sub-transaction, establish new state and relay.
		 */
		lstate = lmsg->state;
		lpstate = lstate->parent;
		rpstate = lpstate->relay;
		assert(lstate->relay == NULL);
		assert(rpstate != NULL);

		rmsg = dmsg_msg_alloc(rpstate,
				      lmsg->aux_size,
				      lmsg->any.head.cmd,
				      dmsg_state_relay, NULL);
		rstate = rmsg->state;
		rstate->relay = lstate;
		lstate->relay = rstate;
		atomic_add_int(&lstate->refs, 1);
		atomic_add_int(&rstate->refs, 1);
	} else {
		/*
		 * State & relay already established
		 */
		lstate = lmsg->state;
		rstate = lstate->relay;
		assert(rstate != NULL);

		rmsg = dmsg_msg_alloc(rstate,
				      lmsg->aux_size,
				      lmsg->any.head.cmd,
				      dmsg_state_relay, NULL);
	}
	if (lmsg->hdr_size > sizeof(lmsg->any.head)) {
		bcopy(&lmsg->any.head + 1, &rmsg->any.head + 1,
		      lmsg->hdr_size - sizeof(lmsg->any.head));
	}
	rmsg->any.head.error = lmsg->any.head.error;
	rmsg->any.head.reserved02 = lmsg->any.head.reserved02;
	rmsg->any.head.reserved18 = lmsg->any.head.reserved18;
	rmsg->aux_data = lmsg->aux_data;
	lmsg->aux_data = NULL;
	/*
	fprintf(stderr, "RELAY %08x\n", rmsg->any.head.cmd);
	*/
	dmsg_msg_write(rmsg);
}

/*
 * Cleanup and retire msg after processing
 */
void
dmsg_state_cleanuprx(dmsg_iocom_t *iocom, dmsg_msg_t *msg)
{
	dmsg_state_t *state;
	dmsg_state_t *pstate;

	assert(msg->state->iocom == iocom);
	state = msg->state;
	if (state->flags & DMSG_STATE_ROOT) {
		/*
		 * Free a non-transactional message, there is no state
		 * to worry about.
		 */
		dmsg_msg_free(msg);
	} else if (msg->any.head.cmd & DMSGF_DELETE) {
		/*
		 * Message terminating transaction, destroy the related
		 * state, the original message, and this message (if it
		 * isn't the original message due to a CREATE|DELETE).
		 */
		pthread_mutex_lock(&iocom->mtx);
		state->rxcmd |= DMSGF_DELETE;
		if (state->txcmd & DMSGF_DELETE) {
			assert(state->flags & DMSG_STATE_INSERTED);
			assert(TAILQ_EMPTY(&state->subq));
			if (state->rxcmd & DMSGF_REPLY) {
				assert(msg->any.head.cmd & DMSGF_REPLY);
				RB_REMOVE(dmsg_state_tree,
					  &iocom->statewr_tree, state);
			} else {
				assert((msg->any.head.cmd & DMSGF_REPLY) == 0);
				RB_REMOVE(dmsg_state_tree,
					  &iocom->staterd_tree, state);
			}
			pstate = state->parent;
			TAILQ_REMOVE(&pstate->subq, state, entry);
			state->flags &= ~DMSG_STATE_INSERTED;
			state->parent = NULL;
			atomic_add_int(&pstate->refs, -1);

			if ((pstate->flags & DMSG_STATE_ROOT) == 0 &&
			    TAILQ_EMPTY(&pstate->subq) &&
			    (pstate->flags & DMSG_STATE_INSERTED) == 0) {
				dmsg_state_free(pstate);
			}

			if (state->relay) {
				atomic_add_int(&state->relay->refs, -1);
				state->relay = NULL;
			}
			dmsg_msg_free(msg);
			if (TAILQ_EMPTY(&state->subq))
				dmsg_state_free(state);
		} else {
			dmsg_msg_free(msg);
		}
		pthread_mutex_unlock(&iocom->mtx);
	} else {
		/*
		 * Message not terminating transaction, leave state intact
		 * and free message if it isn't the CREATE message.
		 */
		dmsg_msg_free(msg);
	}
}

static void
dmsg_state_cleanuptx(dmsg_iocom_t *iocom, dmsg_msg_t *msg)
{
	dmsg_state_t *state;
	dmsg_state_t *pstate;

	assert(iocom == msg->state->iocom);
	state = msg->state;
	if (state->flags & DMSG_STATE_ROOT) {
		dmsg_msg_free(msg);
	} else if (msg->any.head.cmd & DMSGF_DELETE) {
		pthread_mutex_lock(&iocom->mtx);
		assert((state->txcmd & DMSGF_DELETE) == 0);
		state->txcmd |= DMSGF_DELETE;
		if (state->rxcmd & DMSGF_DELETE) {
			assert(state->flags & DMSG_STATE_INSERTED);
			assert(TAILQ_EMPTY(&state->subq));
			if (state->txcmd & DMSGF_REPLY) {
				assert(msg->any.head.cmd & DMSGF_REPLY);
				RB_REMOVE(dmsg_state_tree,
					  &iocom->staterd_tree, state);
			} else {
				assert((msg->any.head.cmd & DMSGF_REPLY) == 0);
				RB_REMOVE(dmsg_state_tree,
					  &iocom->statewr_tree, state);
			}
			pstate = state->parent;
			TAILQ_REMOVE(&pstate->subq, state, entry);
			state->flags &= ~DMSG_STATE_INSERTED;
			state->parent = NULL;
			atomic_add_int(&pstate->refs, -1);

			if ((pstate->flags & DMSG_STATE_ROOT) == 0 &&
			    TAILQ_EMPTY(&pstate->subq) &&
			    (pstate->flags & DMSG_STATE_INSERTED) == 0) {
				dmsg_state_free(pstate);
			}

			if (state->relay) {
				atomic_add_int(&state->relay->refs, -1);
				state->relay = NULL;
			}
			dmsg_msg_free(msg);
			if (TAILQ_EMPTY(&state->subq))
				dmsg_state_free(state);
		} else {
			dmsg_msg_free(msg);
		}
		pthread_mutex_unlock(&iocom->mtx);
	} else {
		dmsg_msg_free(msg);
	}
}

/*
 * Called with iocom locked
 */
void
dmsg_state_free(dmsg_state_t *state)
{
	if (DMsgDebugOpt) {
		fprintf(stderr, "terminate state %p id=%08x\n",
			state, (uint32_t)state->msgid);
	}
	if (state->any.any != NULL)   /* XXX avoid deadlock w/exit & kernel */
		closefrom(3);
	assert(state->any.any == NULL);
	free(state);
}

/*
 * This swaps endian for a hammer2_msg_hdr.  Note that the extended
 * header is not adjusted, just the core header.
 */
void
dmsg_bswap_head(dmsg_hdr_t *head)
{
	head->magic	= bswap16(head->magic);
	head->reserved02 = bswap16(head->reserved02);
	head->salt	= bswap32(head->salt);

	head->msgid	= bswap64(head->msgid);
	head->circuit	= bswap64(head->circuit);
	head->reserved18= bswap64(head->reserved18);

	head->cmd	= bswap32(head->cmd);
	head->aux_crc	= bswap32(head->aux_crc);
	head->aux_bytes	= bswap32(head->aux_bytes);
	head->error	= bswap32(head->error);
	head->aux_descr = bswap64(head->aux_descr);
	head->reserved38= bswap32(head->reserved38);
	head->hdr_crc	= bswap32(head->hdr_crc);
}
