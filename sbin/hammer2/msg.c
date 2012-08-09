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

#include "hammer2.h"

static int hammer2_state_msgrx(hammer2_iocom_t *iocom, hammer2_msg_t *msg);
static void hammer2_state_cleanuptx(hammer2_iocom_t *iocom, hammer2_msg_t *msg);

/*
 * Indexed messages are stored in a red-black tree indexed by their
 * msgid.  Only persistent messages are indexed.
 */
int
hammer2_state_cmp(hammer2_state_t *state1, hammer2_state_t *state2)
{
	if (state1->spanid < state2->spanid)
		return(-1);
	if (state1->spanid > state2->spanid)
		return(1);
	if (state1->msgid < state2->msgid)
		return(-1);
	if (state1->msgid > state2->msgid)
		return(1);
	return(0);
}

RB_GENERATE(hammer2_state_tree, hammer2_state, rbnode, hammer2_state_cmp);

/*
 * Initialize a low-level ioq
 */
void
hammer2_ioq_init(hammer2_iocom_t *iocom __unused, hammer2_ioq_t *ioq)
{
	bzero(ioq, sizeof(*ioq));
	ioq->state = HAMMER2_MSGQ_STATE_HEADER1;
	TAILQ_INIT(&ioq->msgq);
}

/*
 * Cleanup queue.
 *
 * caller holds iocom->mtx.
 */
void
hammer2_ioq_done(hammer2_iocom_t *iocom __unused, hammer2_ioq_t *ioq)
{
	hammer2_msg_t *msg;

	while ((msg = TAILQ_FIRST(&ioq->msgq)) != NULL) {
		assert(0);	/* shouldn't happen */
		TAILQ_REMOVE(&ioq->msgq, msg, qentry);
		hammer2_msg_free(msg);
	}
	if ((msg = ioq->msg) != NULL) {
		ioq->msg = NULL;
		hammer2_msg_free(msg);
	}
}

/*
 * Initialize a low-level communications channel.
 *
 * NOTE: The signal_func() is called at least once from the loop and can be
 *	 re-armed via hammer2_iocom_restate().
 */
void
hammer2_iocom_init(hammer2_iocom_t *iocom, int sock_fd, int alt_fd,
		   void (*signal_func)(hammer2_router_t *),
		   void (*rcvmsg_func)(hammer2_msg_t *),
		   void (*altmsg_func)(hammer2_iocom_t *))
{
	bzero(iocom, sizeof(*iocom));

	iocom->router.signal_callback = signal_func;
	iocom->router.rcvmsg_callback = rcvmsg_func;
	iocom->router.altmsg_callback = altmsg_func;

	pthread_mutex_init(&iocom->mtx, NULL);
	RB_INIT(&iocom->router.staterd_tree);
	RB_INIT(&iocom->router.statewr_tree);
	TAILQ_INIT(&iocom->freeq);
	TAILQ_INIT(&iocom->freeq_aux);
	TAILQ_INIT(&iocom->addrq);
	TAILQ_INIT(&iocom->router.txmsgq);
	iocom->router.iocom = iocom;
	iocom->sock_fd = sock_fd;
	iocom->alt_fd = alt_fd;
	iocom->flags = HAMMER2_IOCOMF_RREQ;
	if (signal_func)
		iocom->flags |= HAMMER2_IOCOMF_SWORK;
	hammer2_ioq_init(iocom, &iocom->ioq_rx);
	hammer2_ioq_init(iocom, &iocom->ioq_tx);
	if (pipe(iocom->wakeupfds) < 0)
		assert(0);
	fcntl(iocom->wakeupfds[0], F_SETFL, O_NONBLOCK);
	fcntl(iocom->wakeupfds[1], F_SETFL, O_NONBLOCK);

	/*
	 * Negotiate session crypto synchronously.  This will mark the
	 * connection as error'd if it fails.
	 */
	hammer2_crypto_negotiate(iocom);

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

/*
 * May only be called from a callback from iocom_core.
 *
 * Adjust state machine functions, set flags to guarantee that both
 * the recevmsg_func and the sendmsg_func is called at least once.
 */
void
hammer2_router_restate(hammer2_router_t *router,
		   void (*signal_func)(hammer2_router_t *),
		   void (*rcvmsg_func)(hammer2_msg_t *msg),
		   void (*altmsg_func)(hammer2_iocom_t *))
{
	router->signal_callback = signal_func;
	router->rcvmsg_callback = rcvmsg_func;
	router->altmsg_callback = altmsg_func;
	if (signal_func)
		router->iocom->flags |= HAMMER2_IOCOMF_SWORK;
	else
		router->iocom->flags &= ~HAMMER2_IOCOMF_SWORK;
}

void
hammer2_router_signal(hammer2_router_t *router)
{
	if (router->signal_callback)
		router->iocom->flags |= HAMMER2_IOCOMF_SWORK;
}

/*
 * Cleanup a terminating iocom.
 *
 * Caller should not hold iocom->mtx.  The iocom has already been disconnected
 * from all possible references to it.
 */
void
hammer2_iocom_done(hammer2_iocom_t *iocom)
{
	hammer2_msg_t *msg;

	if (iocom->sock_fd >= 0) {
		close(iocom->sock_fd);
		iocom->sock_fd = -1;
	}
	if (iocom->alt_fd >= 0) {
		close(iocom->alt_fd);
		iocom->alt_fd = -1;
	}
	hammer2_ioq_done(iocom, &iocom->ioq_rx);
	hammer2_ioq_done(iocom, &iocom->ioq_tx);
	if ((msg = TAILQ_FIRST(&iocom->freeq)) != NULL) {
		TAILQ_REMOVE(&iocom->freeq, msg, qentry);
		free(msg);
	}
	if ((msg = TAILQ_FIRST(&iocom->freeq_aux)) != NULL) {
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
 * Allocate a new one-way message.
 */
hammer2_msg_t *
hammer2_msg_alloc(hammer2_router_t *router, size_t aux_size, uint32_t cmd,
		  void (*func)(hammer2_msg_t *), void *data)
{
	hammer2_state_t *state = NULL;
	hammer2_iocom_t *iocom = router->iocom;
	hammer2_msg_t *msg;
	int hbytes;

	pthread_mutex_lock(&iocom->mtx);
	if (aux_size) {
		aux_size = (aux_size + HAMMER2_MSG_ALIGNMASK) &
			   ~HAMMER2_MSG_ALIGNMASK;
		if ((msg = TAILQ_FIRST(&iocom->freeq_aux)) != NULL)
			TAILQ_REMOVE(&iocom->freeq_aux, msg, qentry);
	} else {
		if ((msg = TAILQ_FIRST(&iocom->freeq)) != NULL)
			TAILQ_REMOVE(&iocom->freeq, msg, qentry);
	}
	if ((cmd & (HAMMER2_MSGF_CREATE | HAMMER2_MSGF_REPLY)) ==
	    HAMMER2_MSGF_CREATE) {
		/*
		 * Create state when CREATE is set without REPLY.
		 *
		 * NOTE: CREATE in txcmd handled by hammer2_msg_write()
		 * NOTE: DELETE in txcmd handled by hammer2_state_cleanuptx()
		 */
		state = malloc(sizeof(*state));
		bzero(state, sizeof(*state));
		state->iocom = iocom;
		state->flags = HAMMER2_STATE_DYNAMIC;
		state->msgid = (uint64_t)(uintptr_t)state;
		/* XXX set state->spanid from router */
		state->txcmd = cmd & ~(HAMMER2_MSGF_CREATE |
				       HAMMER2_MSGF_DELETE);
		state->rxcmd = HAMMER2_MSGF_REPLY;
		state->func = func;
		state->any.any = data;
		pthread_mutex_lock(&iocom->mtx);
		RB_INSERT(hammer2_state_tree, &iocom->router.statewr_tree, state);
		pthread_mutex_unlock(&iocom->mtx);
		state->flags |= HAMMER2_STATE_INSERTED;
	}
	pthread_mutex_unlock(&iocom->mtx);
	if (msg == NULL) {
		msg = malloc(sizeof(*msg));
		bzero(msg, sizeof(*msg));
		msg->aux_data = NULL;
		msg->aux_size = 0;
	}
	if (msg->aux_size != aux_size) {
		if (msg->aux_data) {
			free(msg->aux_data);
			msg->aux_data = NULL;
			msg->aux_size = 0;
		}
		if (aux_size) {
			msg->aux_data = malloc(aux_size);
			msg->aux_size = aux_size;
		}
	}
	hbytes = (cmd & HAMMER2_MSGF_SIZE) * HAMMER2_MSG_ALIGN;
	if (hbytes)
		bzero(&msg->any.head, hbytes);
	msg->hdr_size = hbytes;
	msg->any.head.cmd = cmd;
	msg->any.head.aux_descr = 0;
	msg->any.head.aux_crc = 0;
	msg->router = router;
	if (state) {
		msg->state = state;
		state->msg = msg;
		msg->any.head.msgid = state->msgid;
	}
	return (msg);
}

/*
 * Free a message so it can be reused afresh.
 *
 * NOTE: aux_size can be 0 with a non-NULL aux_data.
 */
static
void
hammer2_msg_free_locked(hammer2_msg_t *msg)
{
	hammer2_iocom_t *iocom = msg->router->iocom;

	msg->state = NULL;
	if (msg->aux_data)
		TAILQ_INSERT_TAIL(&iocom->freeq_aux, msg, qentry);
	else
		TAILQ_INSERT_TAIL(&iocom->freeq, msg, qentry);
}

void
hammer2_msg_free(hammer2_msg_t *msg)
{
	hammer2_iocom_t *iocom = msg->router->iocom;

	pthread_mutex_lock(&iocom->mtx);
	hammer2_msg_free_locked(msg);
	pthread_mutex_unlock(&iocom->mtx);
}

/*
 * I/O core loop for an iocom.
 *
 * Thread localized, iocom->mtx not held.
 */
void
hammer2_iocom_core(hammer2_iocom_t *iocom)
{
	struct pollfd fds[3];
	char dummybuf[256];
	hammer2_msg_t *msg;
	int timeout;
	int count;
	int wi;	/* wakeup pipe */
	int si;	/* socket */
	int ai;	/* alt bulk path socket */

	while ((iocom->flags & HAMMER2_IOCOMF_EOF) == 0) {
		if ((iocom->flags & (HAMMER2_IOCOMF_RWORK |
				     HAMMER2_IOCOMF_WWORK |
				     HAMMER2_IOCOMF_PWORK |
				     HAMMER2_IOCOMF_SWORK |
				     HAMMER2_IOCOMF_ARWORK |
				     HAMMER2_IOCOMF_AWWORK)) == 0) {
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
			if (iocom->flags & (HAMMER2_IOCOMF_RREQ |
					    HAMMER2_IOCOMF_WREQ)) {
				si = count++;
				fds[si].fd = iocom->sock_fd;
				fds[si].events = 0;
				fds[si].revents = 0;

				if (iocom->flags & HAMMER2_IOCOMF_RREQ)
					fds[si].events |= POLLIN;
				if (iocom->flags & HAMMER2_IOCOMF_WREQ)
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
				iocom->flags |= HAMMER2_IOCOMF_PWORK;
			if (si >= 0 && (fds[si].revents & POLLIN))
				iocom->flags |= HAMMER2_IOCOMF_RWORK;
			if (si >= 0 && (fds[si].revents & POLLOUT))
				iocom->flags |= HAMMER2_IOCOMF_WWORK;
			if (wi >= 0 && (fds[wi].revents & POLLOUT))
				iocom->flags |= HAMMER2_IOCOMF_WWORK;
			if (ai >= 0 && (fds[ai].revents & POLLIN))
				iocom->flags |= HAMMER2_IOCOMF_ARWORK;
		} else {
			/*
			 * Always check the pipe
			 */
			iocom->flags |= HAMMER2_IOCOMF_PWORK;
		}

		if (iocom->flags & HAMMER2_IOCOMF_SWORK) {
			iocom->flags &= ~HAMMER2_IOCOMF_SWORK;
			iocom->router.signal_callback(&iocom->router);
		}

		/*
		 * Pending message queues from other threads wake us up
		 * with a write to the wakeupfds[] pipe.  We have to clear
		 * the pipe with a dummy read.
		 */
		if (iocom->flags & HAMMER2_IOCOMF_PWORK) {
			iocom->flags &= ~HAMMER2_IOCOMF_PWORK;
			read(iocom->wakeupfds[0], dummybuf, sizeof(dummybuf));
			iocom->flags |= HAMMER2_IOCOMF_RWORK;
			iocom->flags |= HAMMER2_IOCOMF_WWORK;
			if (TAILQ_FIRST(&iocom->router.txmsgq))
				hammer2_iocom_flush1(iocom);
		}

		/*
		 * Message write sequencing
		 */
		if (iocom->flags & HAMMER2_IOCOMF_WWORK)
			hammer2_iocom_flush1(iocom);

		/*
		 * Message read sequencing.  Run this after the write
		 * sequencing in case the write sequencing allowed another
		 * auto-DELETE to occur on the read side.
		 */
		if (iocom->flags & HAMMER2_IOCOMF_RWORK) {
			while ((iocom->flags & HAMMER2_IOCOMF_EOF) == 0 &&
			       (msg = hammer2_ioq_read(iocom)) != NULL) {
				if (DebugOpt) {
					fprintf(stderr, "receive %s\n",
						hammer2_msg_str(msg));
				}
				iocom->router.rcvmsg_callback(msg);
				hammer2_state_cleanuprx(iocom, msg);
			}
		}

		if (iocom->flags & HAMMER2_IOCOMF_ARWORK) {
			iocom->flags &= ~HAMMER2_IOCOMF_ARWORK;
			iocom->router.altmsg_callback(iocom);
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
hammer2_ioq_makeroom(hammer2_ioq_t *ioq, size_t needed)
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
 * If an error occurs during reception a HAMMER2_LNK_ERROR msg will
 * be returned for each open transaction, then the ioq and iocom
 * will be errored out and a non-transactional HAMMER2_LNK_ERROR
 * msg will be returned as the final message.  The caller should not call
 * us again after the final message is returned.
 *
 * Thread localized, iocom->mtx not held.
 */
hammer2_msg_t *
hammer2_ioq_read(hammer2_iocom_t *iocom)
{
	hammer2_ioq_t *ioq = &iocom->ioq_rx;
	hammer2_msg_t *msg;
	hammer2_msg_hdr_t *head;
	hammer2_state_t *state;
	ssize_t n;
	size_t bytes;
	size_t nmax;
	uint32_t xcrc32;
	int error;

again:
	iocom->flags &= ~(HAMMER2_IOCOMF_RREQ | HAMMER2_IOCOMF_RWORK);

	/*
	 * If a message is already pending we can just remove and
	 * return it.  Message state has already been processed.
	 */
	if ((msg = TAILQ_FIRST(&ioq->msgq)) != NULL) {
		TAILQ_REMOVE(&ioq->msgq, msg, qentry);
		return (msg);
	}

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
	case HAMMER2_MSGQ_STATE_HEADER1:
		/*
		 * Load the primary header, fail on any non-trivial read
		 * error or on EOF.  Since the primary header is the same
		 * size is the message alignment it will never straddle
		 * the end of the buffer.
		 */
		nmax = hammer2_ioq_makeroom(ioq, sizeof(msg->any.head));
		if (bytes < sizeof(msg->any.head)) {
			n = read(iocom->sock_fd,
				 ioq->buf + ioq->fifo_end,
				 nmax);
			if (n <= 0) {
				if (n == 0) {
					ioq->error = HAMMER2_IOQ_ERROR_EOF;
					break;
				}
				if (errno != EINTR &&
				    errno != EINPROGRESS &&
				    errno != EAGAIN) {
					ioq->error = HAMMER2_IOQ_ERROR_SOCK;
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
		if (iocom->flags & HAMMER2_IOCOMF_CRYPTED) {
			hammer2_crypto_decrypt(iocom, ioq);
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
		if (head->magic != HAMMER2_MSGHDR_MAGIC &&
		    head->magic != HAMMER2_MSGHDR_MAGIC_REV) {
			ioq->error = HAMMER2_IOQ_ERROR_SYNC;
			break;
		}

		/*
		 * Calculate the full header size and aux data size
		 */
		if (head->magic == HAMMER2_MSGHDR_MAGIC_REV) {
			ioq->hbytes = (bswap32(head->cmd) & HAMMER2_MSGF_SIZE) *
				      HAMMER2_MSG_ALIGN;
			ioq->abytes = bswap32(head->aux_bytes) *
				      HAMMER2_MSG_ALIGN;
		} else {
			ioq->hbytes = (head->cmd & HAMMER2_MSGF_SIZE) *
				      HAMMER2_MSG_ALIGN;
			ioq->abytes = head->aux_bytes * HAMMER2_MSG_ALIGN;
		}
		if (ioq->hbytes < sizeof(msg->any.head) ||
		    ioq->hbytes > sizeof(msg->any) ||
		    ioq->abytes > HAMMER2_MSGAUX_MAX) {
			ioq->error = HAMMER2_IOQ_ERROR_FIELD;
			break;
		}

		/*
		 * Allocate the message, the next state will fill it in.
		 */
		msg = hammer2_msg_alloc(&iocom->router, ioq->abytes, 0,
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
		nmax = hammer2_ioq_makeroom(ioq, ioq->hbytes);
		ioq->state = HAMMER2_MSGQ_STATE_HEADER2;
		/* fall through */
	case HAMMER2_MSGQ_STATE_HEADER2:
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
					ioq->error = HAMMER2_IOQ_ERROR_EOF;
					break;
				}
				if (errno != EINTR &&
				    errno != EINPROGRESS &&
				    errno != EAGAIN) {
					ioq->error = HAMMER2_IOQ_ERROR_SOCK;
					break;
				}
				n = 0;
				/* fall through */
			}
			ioq->fifo_end += (size_t)n;
			nmax -= (size_t)n;
		}

		if (iocom->flags & HAMMER2_IOCOMF_CRYPTED) {
			hammer2_crypto_decrypt(iocom, ioq);
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
		if (head->magic == HAMMER2_MSGHDR_MAGIC_REV)
			xcrc32 = bswap32(head->hdr_crc);
		else
			xcrc32 = head->hdr_crc;
		head->hdr_crc = 0;
		if (hammer2_icrc32(head, ioq->hbytes) != xcrc32) {
			ioq->error = HAMMER2_IOQ_ERROR_XCRC;
			fprintf(stderr, "BAD-XCRC(%08x,%08x) %s\n",
				xcrc32, hammer2_icrc32(head, ioq->hbytes),
				hammer2_msg_str(msg));
			assert(0);
			break;
		}
		head->hdr_crc = xcrc32;

		if (head->magic == HAMMER2_MSGHDR_MAGIC_REV) {
			hammer2_bswap_head(head);
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
		ioq->state = HAMMER2_MSGQ_STATE_AUXDATA1;
		/* fall through */
	case HAMMER2_MSGQ_STATE_AUXDATA1:
		/*
		 * Copy the partial or complete payload from remaining
		 * bytes in the FIFO in order to optimize the makeroom call
		 * in the AUXDATA2 state.  We have to fall-through either
		 * way so we can check the crc.
		 *
		 * msg->aux_size tracks our aux data.
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
		ioq->state = HAMMER2_MSGQ_STATE_AUXDATA2;
		/* fall through */
	case HAMMER2_MSGQ_STATE_AUXDATA2:
		/*
		 * Make sure there is enough room for more data.
		 */
		assert(msg);
		nmax = hammer2_ioq_makeroom(ioq, ioq->abytes - msg->aux_size);

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
					ioq->error = HAMMER2_IOQ_ERROR_EOF;
					break;
				}
				if (errno != EINTR &&
				    errno != EINPROGRESS &&
				    errno != EAGAIN) {
					ioq->error = HAMMER2_IOQ_ERROR_SOCK;
					break;
				}
				n = 0;
				/* fall through */
			}
			ioq->fifo_end += (size_t)n;
			nmax -= (size_t)n;
		}

		if (iocom->flags & HAMMER2_IOCOMF_CRYPTED) {
			hammer2_crypto_decrypt(iocom, ioq);
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
		 */
		if (msg->aux_size < ioq->abytes) {
			msg = NULL;
			break;
		}
		assert(msg->aux_size == ioq->abytes);

		/*
		 * Check aux_crc, then we are done.
		 */
		xcrc32 = hammer2_icrc32(msg->aux_data, msg->aux_size);
		if (xcrc32 != msg->any.head.aux_crc) {
			ioq->error = HAMMER2_IOQ_ERROR_ACRC;
			break;
		}
		break;
	case HAMMER2_MSGQ_STATE_ERROR:
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
			ioq->error = HAMMER2_IOQ_ERROR_MSGSEQ;
		} else {
			++ioq->seq;
		}
	}

	/*
	 * Process transactional state for the message.
	 */
	if (msg && ioq->error == 0) {
		error = hammer2_state_msgrx(iocom, msg);
		if (error) {
			if (error == HAMMER2_IOQ_ERROR_EALREADY) {
				hammer2_msg_free(msg);
				goto again;
			}
			ioq->error = error;
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
			hammer2_msg_free(msg);
			ioq->msg = NULL;
		}

		/*
		 * No more I/O read processing
		 */
		ioq->state = HAMMER2_MSGQ_STATE_ERROR;

		/*
		 * Simulate a remote LNK_ERROR DELETE msg for any open
		 * transactions, ending with a final non-transactional
		 * LNK_ERROR (that the session can detect) when no
		 * transactions remain.
		 */
		msg = hammer2_msg_alloc(&iocom->router, 0, 0, NULL, NULL);
		bzero(&msg->any.head, sizeof(msg->any.head));
		msg->any.head.magic = HAMMER2_MSGHDR_MAGIC;
		msg->any.head.cmd = HAMMER2_LNK_ERROR;
		msg->any.head.error = ioq->error;

		pthread_mutex_lock(&iocom->mtx);
		hammer2_iocom_drain(iocom);
		if ((state = RB_ROOT(&iocom->router.staterd_tree)) != NULL) {
			/*
			 * Active remote transactions are still present.
			 * Simulate the other end sending us a DELETE.
			 */
			if (state->rxcmd & HAMMER2_MSGF_DELETE) {
				hammer2_msg_free(msg);
				msg = NULL;
			} else {
				/*state->txcmd |= HAMMER2_MSGF_DELETE;*/
				msg->state = state;
				msg->any.head.spanid = state->spanid;
				msg->any.head.msgid = state->msgid;
				msg->any.head.cmd |= HAMMER2_MSGF_ABORT |
						     HAMMER2_MSGF_DELETE;
			}
		} else if ((state = RB_ROOT(&iocom->router.statewr_tree)) != NULL) {
			/*
			 * Active local transactions are still present.
			 * Simulate the other end sending us a DELETE.
			 */
			if (state->rxcmd & HAMMER2_MSGF_DELETE) {
				hammer2_msg_free(msg);
				msg = NULL;
			} else {
				msg->state = state;
				msg->any.head.spanid = state->spanid;
				msg->any.head.msgid = state->msgid;
				msg->any.head.cmd |= HAMMER2_MSGF_ABORT |
						     HAMMER2_MSGF_DELETE |
						     HAMMER2_MSGF_REPLY;
				if ((state->rxcmd & HAMMER2_MSGF_CREATE) == 0) {
					msg->any.head.cmd |=
						     HAMMER2_MSGF_CREATE;
				}
			}
		} else {
			/*
			 * No active local or remote transactions remain.
			 * Generate a final LNK_ERROR and flag EOF.
			 */
			msg->state = NULL;
			iocom->flags |= HAMMER2_IOCOMF_EOF;
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
			iocom->flags |= HAMMER2_IOCOMF_RWORK;
	} else if (msg == NULL) {
		/*
		 * Insufficient data received to finish building the message,
		 * set RREQ and return NULL.
		 *
		 * Leave ioq->msg intact.
		 * Leave the FIFO intact.
		 */
		iocom->flags |= HAMMER2_IOCOMF_RREQ;
	} else {
		/*
		 * Return msg.
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
			iocom->flags |= HAMMER2_IOCOMF_RREQ;
			ioq->fifo_cdx = 0;
			ioq->fifo_cdn = 0;
			ioq->fifo_beg = 0;
			ioq->fifo_end = 0;
		} else {
			iocom->flags |= HAMMER2_IOCOMF_RWORK;
		}
		ioq->state = HAMMER2_MSGQ_STATE_HEADER1;
		ioq->msg = NULL;
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
 * Caller must hold iocom->mtx.
 */
void
hammer2_iocom_flush1(hammer2_iocom_t *iocom)
{
	hammer2_ioq_t *ioq = &iocom->ioq_tx;
	hammer2_msg_t *msg;
	uint32_t xcrc32;
	int hbytes;
	hammer2_msg_queue_t tmpq;

	iocom->flags &= ~(HAMMER2_IOCOMF_WREQ | HAMMER2_IOCOMF_WWORK);
	TAILQ_INIT(&tmpq);
	pthread_mutex_lock(&iocom->mtx);
	while ((msg = TAILQ_FIRST(&iocom->router.txmsgq)) != NULL) {
		TAILQ_REMOVE(&iocom->router.txmsgq, msg, qentry);
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
		msg->any.head.magic = HAMMER2_MSGHDR_MAGIC;
		msg->any.head.salt = (random() << 8) | (ioq->seq & 255);
		++ioq->seq;
		if ((ioq->seq & 32767) == 0)
			srandomdev();

		/*
		 * Calculate aux_crc if 0, then calculate hdr_crc.
		 */
		if (msg->aux_size && msg->any.head.aux_crc == 0) {
			assert((msg->aux_size & HAMMER2_MSG_ALIGNMASK) == 0);
			xcrc32 = hammer2_icrc32(msg->aux_data, msg->aux_size);
			msg->any.head.aux_crc = xcrc32;
		}
		msg->any.head.aux_bytes = msg->aux_size / HAMMER2_MSG_ALIGN;
		assert((msg->aux_size & HAMMER2_MSG_ALIGNMASK) == 0);

		hbytes = (msg->any.head.cmd & HAMMER2_MSGF_SIZE) *
			 HAMMER2_MSG_ALIGN;
		msg->any.head.hdr_crc = 0;
		msg->any.head.hdr_crc = hammer2_icrc32(&msg->any.head, hbytes);

		/*
		 * Enqueue the message (the flush codes handles stream
		 * encryption).
		 */
		TAILQ_INSERT_TAIL(&ioq->msgq, msg, qentry);
		++ioq->msgcount;
	}
	hammer2_iocom_flush2(iocom);
}

/*
 * Thread localized, iocom->mtx not held by caller.
 */
void
hammer2_iocom_flush2(hammer2_iocom_t *iocom)
{
	hammer2_ioq_t *ioq = &iocom->ioq_tx;
	hammer2_msg_t *msg;
	ssize_t n;
	struct iovec iov[HAMMER2_IOQ_MAXIOVEC];
	size_t nact;
	size_t hbytes;
	size_t abytes;
	size_t hoff;
	size_t aoff;
	int iovcnt;

	if (ioq->error) {
		hammer2_iocom_drain(iocom);
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
		hbytes = (msg->any.head.cmd & HAMMER2_MSGF_SIZE) *
			 HAMMER2_MSG_ALIGN;
		abytes = msg->aux_size;
		assert(hoff <= hbytes && aoff <= abytes);

		if (hoff < hbytes) {
			iov[iovcnt].iov_base = (char *)&msg->any.head + hoff;
			iov[iovcnt].iov_len = hbytes - hoff;
			nact += hbytes - hoff;
			++iovcnt;
			if (iovcnt == HAMMER2_IOQ_MAXIOVEC)
				break;
		}
		if (aoff < abytes) {
			assert(msg->aux_data != NULL);
			iov[iovcnt].iov_base = (char *)msg->aux_data + aoff;
			iov[iovcnt].iov_len = abytes - aoff;
			nact += abytes - aoff;
			++iovcnt;
			if (iovcnt == HAMMER2_IOQ_MAXIOVEC)
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
	if (iocom->flags & HAMMER2_IOCOMF_CRYPTED) {
		/*
		 * Make sure the FIFO has a reasonable amount of space
		 * left (if not completely full).
		 */
		if (ioq->fifo_beg > sizeof(ioq->buf) / 2 &&
		    sizeof(ioq->buf) - ioq->fifo_end >= HAMMER2_MSG_ALIGN * 2) {
			bcopy(ioq->buf + ioq->fifo_beg, ioq->buf,
			      ioq->fifo_end - ioq->fifo_beg);
			ioq->fifo_cdx -= ioq->fifo_beg;
			ioq->fifo_cdn -= ioq->fifo_beg;
			ioq->fifo_end -= ioq->fifo_beg;
			ioq->fifo_beg = 0;
		}

		iovcnt = hammer2_crypto_encrypt(iocom, ioq, iov, iovcnt, &nact);
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
	} else {
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
		hbytes = (msg->any.head.cmd & HAMMER2_MSGF_SIZE) *
			 HAMMER2_MSG_ALIGN;
		abytes = msg->aux_size;

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

		TAILQ_REMOVE(&ioq->msgq, msg, qentry);
		--ioq->msgcount;
		ioq->hbytes = 0;
		ioq->abytes = 0;

		hammer2_state_cleanuptx(iocom, msg);
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
			ioq->error = HAMMER2_IOQ_ERROR_SOCK;
			hammer2_iocom_drain(iocom);
		} else {
			/*
			 * Wait for socket buffer space
			 */
			iocom->flags |= HAMMER2_IOCOMF_WREQ;
		}
	} else {
		iocom->flags |= HAMMER2_IOCOMF_WREQ;
	}
	if (ioq->error) {
		hammer2_iocom_drain(iocom);
	}
}

/*
 * Kill pending msgs on ioq_tx and adjust the flags such that no more
 * write events will occur.  We don't kill read msgs because we want
 * the caller to pull off our contrived terminal error msg to detect
 * the connection failure.
 *
 * Thread localized, iocom->mtx not held by caller.
 */
void
hammer2_iocom_drain(hammer2_iocom_t *iocom)
{
	hammer2_ioq_t *ioq = &iocom->ioq_tx;
	hammer2_msg_t *msg;

	iocom->flags &= ~(HAMMER2_IOCOMF_WREQ | HAMMER2_IOCOMF_WWORK);
	ioq->hbytes = 0;
	ioq->abytes = 0;

	while ((msg = TAILQ_FIRST(&ioq->msgq)) != NULL) {
		TAILQ_REMOVE(&ioq->msgq, msg, qentry);
		--ioq->msgcount;
		hammer2_state_cleanuptx(iocom, msg);
	}
}

/*
 * Write a message to an iocom, with additional state processing.
 */
void
hammer2_msg_write(hammer2_msg_t *msg)
{
	hammer2_iocom_t *iocom = msg->router->iocom;
	hammer2_state_t *state;
	char dummy;

	/*
	 * Handle state processing, create state if necessary.
	 */
	pthread_mutex_lock(&iocom->mtx);
	if ((state = msg->state) != NULL) {
		/*
		 * Existing transaction (could be reply).  It is also
		 * possible for this to be the first reply (CREATE is set),
		 * in which case we populate state->txcmd.
		 *
		 * state->txcmd is adjusted to hold the final message cmd,
		 * and we also be sure to set the CREATE bit here.  We did
		 * not set it in hammer2_msg_alloc() because that would have
		 * not been serialized (state could have gotten ripped out
		 * from under the message prior to it being transmitted).
		 */
		if ((msg->any.head.cmd & (HAMMER2_MSGF_CREATE |
					  HAMMER2_MSGF_REPLY)) ==
		    HAMMER2_MSGF_CREATE) {
			state->txcmd = msg->any.head.cmd & ~HAMMER2_MSGF_DELETE;
		}
		msg->any.head.msgid = state->msgid;
		msg->any.head.spanid = state->spanid;
		assert(((state->txcmd ^ msg->any.head.cmd) &
			HAMMER2_MSGF_REPLY) == 0);
		if (msg->any.head.cmd & HAMMER2_MSGF_CREATE)
			state->txcmd = msg->any.head.cmd & ~HAMMER2_MSGF_DELETE;
	} else {
		msg->any.head.msgid = 0;
		/* XXX set spanid by router */
	}

	/*
	 * Queue it for output, wake up the I/O pthread.  Note that the
	 * I/O thread is responsible for generating the CRCs and encryption.
	 */
	TAILQ_INSERT_TAIL(&iocom->router.txmsgq, msg, qentry);
	dummy = 0;
	write(iocom->wakeupfds[1], &dummy, 1);	/* XXX optimize me */
	pthread_mutex_unlock(&iocom->mtx);
}

/*
 * This is a shortcut to formulate a reply to msg with a simple error code,
 * It can reply to and terminate a transaction, or it can reply to a one-way
 * messages.  A HAMMER2_LNK_ERROR command code is utilized to encode
 * the error code (which can be 0).  Not all transactions are terminated
 * with HAMMER2_LNK_ERROR status (the low level only cares about the
 * MSGF_DELETE flag), but most are.
 *
 * Replies to one-way messages are a bit of an oxymoron but the feature
 * is used by the debug (DBG) protocol.
 *
 * The reply contains no extended data.
 */
void
hammer2_msg_reply(hammer2_msg_t *msg, uint32_t error)
{
	hammer2_iocom_t *iocom = msg->router->iocom;
	hammer2_state_t *state = msg->state;
	hammer2_msg_t *nmsg;
	uint32_t cmd;


	/*
	 * Reply with a simple error code and terminate the transaction.
	 */
	cmd = HAMMER2_LNK_ERROR;

	/*
	 * Check if our direction has even been initiated yet, set CREATE.
	 *
	 * Check what direction this is (command or reply direction).  Note
	 * that txcmd might not have been initiated yet.
	 *
	 * If our direction has already been closed we just return without
	 * doing anything.
	 */
	if (state) {
		if (state->txcmd & HAMMER2_MSGF_DELETE)
			return;
		if (state->txcmd & HAMMER2_MSGF_REPLY)
			cmd |= HAMMER2_MSGF_REPLY;
		cmd |= HAMMER2_MSGF_DELETE;
	} else {
		if ((msg->any.head.cmd & HAMMER2_MSGF_REPLY) == 0)
			cmd |= HAMMER2_MSGF_REPLY;
	}

	/*
	 * Allocate the message and associate it with the existing state.
	 * We cannot pass MSGF_CREATE to msg_alloc() because that may
	 * allocate new state.  We have our state already.
	 */
	nmsg = hammer2_msg_alloc(&iocom->router, 0, cmd, NULL, NULL);
	if (state) {
		if ((state->txcmd & HAMMER2_MSGF_CREATE) == 0)
			nmsg->any.head.cmd |= HAMMER2_MSGF_CREATE;
	}
	nmsg->any.head.error = error;
	nmsg->state = state;
	hammer2_msg_write(nmsg);
}

/*
 * Similar to hammer2_msg_reply() but leave the transaction open.  That is,
 * we are generating a streaming reply or an intermediate acknowledgement
 * of some sort as part of the higher level protocol, with more to come
 * later.
 */
void
hammer2_msg_result(hammer2_msg_t *msg, uint32_t error)
{
	hammer2_iocom_t *iocom = msg->router->iocom;
	hammer2_state_t *state = msg->state;
	hammer2_msg_t *nmsg;
	uint32_t cmd;


	/*
	 * Reply with a simple error code and terminate the transaction.
	 */
	cmd = HAMMER2_LNK_ERROR;

	/*
	 * Check if our direction has even been initiated yet, set CREATE.
	 *
	 * Check what direction this is (command or reply direction).  Note
	 * that txcmd might not have been initiated yet.
	 *
	 * If our direction has already been closed we just return without
	 * doing anything.
	 */
	if (state) {
		if (state->txcmd & HAMMER2_MSGF_DELETE)
			return;
		if (state->txcmd & HAMMER2_MSGF_REPLY)
			cmd |= HAMMER2_MSGF_REPLY;
		/* continuing transaction, do not set MSGF_DELETE */
	} else {
		if ((msg->any.head.cmd & HAMMER2_MSGF_REPLY) == 0)
			cmd |= HAMMER2_MSGF_REPLY;
	}

	nmsg = hammer2_msg_alloc(&iocom->router, 0, cmd, NULL, NULL);
	if (state) {
		if ((state->txcmd & HAMMER2_MSGF_CREATE) == 0)
			nmsg->any.head.cmd |= HAMMER2_MSGF_CREATE;
	}
	nmsg->any.head.error = error;
	nmsg->state = state;
	hammer2_msg_write(nmsg);
}

/*
 * Terminate a transaction given a state structure by issuing a DELETE.
 */
void
hammer2_state_reply(hammer2_state_t *state, uint32_t error)
{
	hammer2_msg_t *nmsg;
	uint32_t cmd = HAMMER2_LNK_ERROR | HAMMER2_MSGF_DELETE;

	/*
	 * Nothing to do if we already transmitted a delete
	 */
	if (state->txcmd & HAMMER2_MSGF_DELETE)
		return;

	/*
	 * Set REPLY if the other end initiated the command.  Otherwise
	 * we are the command direction.
	 */
	if (state->txcmd & HAMMER2_MSGF_REPLY)
		cmd |= HAMMER2_MSGF_REPLY;

	nmsg = hammer2_msg_alloc(&state->iocom->router, 0, cmd, NULL, NULL);
	if (state) {
		if ((state->txcmd & HAMMER2_MSGF_CREATE) == 0)
			nmsg->any.head.cmd |= HAMMER2_MSGF_CREATE;
	}
	nmsg->any.head.error = error;
	nmsg->state = state;
	hammer2_msg_write(nmsg);
}

/************************************************************************
 *			TRANSACTION STATE HANDLING			*
 ************************************************************************
 *
 */

/*
 * Process state tracking for a message after reception, prior to
 * execution.
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
 * One-off messages (no reply expected) are sent with neither CREATE or DELETE
 * set.  One-off messages cannot be aborted and typically aren't processed
 * by these routines.  The REPLY bit can be used to distinguish whether a
 * one-off message is a command or reply.  For example, one-off replies
 * will typically just contain status updates.
 */
static int
hammer2_state_msgrx(hammer2_iocom_t *iocom, hammer2_msg_t *msg)
{
	hammer2_state_t *state;
	hammer2_state_t dummy;
	int error;

	/*
	 * Lock RB tree and locate existing persistent state, if any.
	 *
	 * If received msg is a command state is on staterd_tree.
	 * If received msg is a reply state is on statewr_tree.
	 */

	dummy.msgid = msg->any.head.msgid;
	dummy.spanid = msg->any.head.spanid;
	pthread_mutex_lock(&iocom->mtx);
	if (msg->any.head.cmd & HAMMER2_MSGF_REPLY) {
		state = RB_FIND(hammer2_state_tree,
				&iocom->router.statewr_tree, &dummy);
	} else {
		state = RB_FIND(hammer2_state_tree,
				&iocom->router.staterd_tree, &dummy);
	}
	msg->state = state;
	pthread_mutex_unlock(&iocom->mtx);

	/*
	 * Short-cut one-off or mid-stream messages (state may be NULL).
	 */
	if ((msg->any.head.cmd & (HAMMER2_MSGF_CREATE | HAMMER2_MSGF_DELETE |
				  HAMMER2_MSGF_ABORT)) == 0) {
		return(0);
	}

	/*
	 * Switch on CREATE, DELETE, REPLY, and also handle ABORT from
	 * inside the case statements.
	 */
	switch(msg->any.head.cmd & (HAMMER2_MSGF_CREATE | HAMMER2_MSGF_DELETE |
				    HAMMER2_MSGF_REPLY)) {
	case HAMMER2_MSGF_CREATE:
	case HAMMER2_MSGF_CREATE | HAMMER2_MSGF_DELETE:
		/*
		 * New persistant command received.
		 */
		if (state) {
			fprintf(stderr, "duplicate-trans %s\n",
				hammer2_msg_str(msg));
			error = HAMMER2_IOQ_ERROR_TRANS;
			assert(0);
			break;
		}
		state = malloc(sizeof(*state));
		bzero(state, sizeof(*state));
		state->iocom = iocom;
		state->flags = HAMMER2_STATE_DYNAMIC;
		state->msg = msg;
		state->txcmd = HAMMER2_MSGF_REPLY;
		state->rxcmd = msg->any.head.cmd & ~HAMMER2_MSGF_DELETE;
		state->flags |= HAMMER2_STATE_INSERTED;
		state->msgid = msg->any.head.msgid;
		state->spanid = msg->any.head.spanid;
		msg->state = state;
		pthread_mutex_lock(&iocom->mtx);
		RB_INSERT(hammer2_state_tree,
			  &iocom->router.staterd_tree, state);
		pthread_mutex_unlock(&iocom->mtx);
		error = 0;
		if (DebugOpt) {
			fprintf(stderr, "create state %p id=%08x on iocom staterd %p\n",
				state, (uint32_t)state->msgid, iocom);
		}
		break;
	case HAMMER2_MSGF_DELETE:
		/*
		 * Persistent state is expected but might not exist if an
		 * ABORT+DELETE races the close.
		 */
		if (state == NULL) {
			if (msg->any.head.cmd & HAMMER2_MSGF_ABORT) {
				error = HAMMER2_IOQ_ERROR_EALREADY;
			} else {
				fprintf(stderr, "missing-state %s\n",
					hammer2_msg_str(msg));
				error = HAMMER2_IOQ_ERROR_TRANS;
			assert(0);
			}
			break;
		}

		/*
		 * Handle another ABORT+DELETE case if the msgid has already
		 * been reused.
		 */
		if ((state->rxcmd & HAMMER2_MSGF_CREATE) == 0) {
			if (msg->any.head.cmd & HAMMER2_MSGF_ABORT) {
				error = HAMMER2_IOQ_ERROR_EALREADY;
			} else {
				fprintf(stderr, "reused-state %s\n",
					hammer2_msg_str(msg));
				error = HAMMER2_IOQ_ERROR_TRANS;
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
		if (msg->any.head.cmd & HAMMER2_MSGF_ABORT) {
			if (state == NULL ||
			    (state->rxcmd & HAMMER2_MSGF_CREATE) == 0) {
				error = HAMMER2_IOQ_ERROR_EALREADY;
				break;
			}
		}
		error = 0;
		break;
	case HAMMER2_MSGF_REPLY | HAMMER2_MSGF_CREATE:
	case HAMMER2_MSGF_REPLY | HAMMER2_MSGF_CREATE | HAMMER2_MSGF_DELETE:
		/*
		 * When receiving a reply with CREATE set the original
		 * persistent state message should already exist.
		 */
		if (state == NULL) {
			fprintf(stderr, "no-state(r) %s\n",
				hammer2_msg_str(msg));
			error = HAMMER2_IOQ_ERROR_TRANS;
			assert(0);
			break;
		}
		assert(((state->rxcmd ^ msg->any.head.cmd) &
			HAMMER2_MSGF_REPLY) == 0);
		state->rxcmd = msg->any.head.cmd & ~HAMMER2_MSGF_DELETE;
		error = 0;
		break;
	case HAMMER2_MSGF_REPLY | HAMMER2_MSGF_DELETE:
		/*
		 * Received REPLY+ABORT+DELETE in case where msgid has
		 * already been fully closed, ignore the message.
		 */
		if (state == NULL) {
			if (msg->any.head.cmd & HAMMER2_MSGF_ABORT) {
				error = HAMMER2_IOQ_ERROR_EALREADY;
			} else {
				fprintf(stderr, "no-state(r,d) %s\n",
					hammer2_msg_str(msg));
				error = HAMMER2_IOQ_ERROR_TRANS;
			assert(0);
			}
			break;
		}

		/*
		 * Received REPLY+ABORT+DELETE in case where msgid has
		 * already been reused for an unrelated message,
		 * ignore the message.
		 */
		if ((state->rxcmd & HAMMER2_MSGF_CREATE) == 0) {
			if (msg->any.head.cmd & HAMMER2_MSGF_ABORT) {
				error = HAMMER2_IOQ_ERROR_EALREADY;
			} else {
				fprintf(stderr, "reused-state(r,d) %s\n",
					hammer2_msg_str(msg));
				error = HAMMER2_IOQ_ERROR_TRANS;
			assert(0);
			}
			break;
		}
		error = 0;
		break;
	case HAMMER2_MSGF_REPLY:
		/*
		 * Check for mid-stream ABORT reply received to sent command.
		 */
		if (msg->any.head.cmd & HAMMER2_MSGF_ABORT) {
			if (state == NULL ||
			    (state->rxcmd & HAMMER2_MSGF_CREATE) == 0) {
				error = HAMMER2_IOQ_ERROR_EALREADY;
				break;
			}
		}
		error = 0;
		break;
	}
	return (error);
}

void
hammer2_state_cleanuprx(hammer2_iocom_t *iocom, hammer2_msg_t *msg)
{
	hammer2_state_t *state;

	if ((state = msg->state) == NULL) {
		/*
		 * Free a non-transactional message, there is no state
		 * to worry about.
		 */
		hammer2_msg_free(msg);
	} else if (msg->any.head.cmd & HAMMER2_MSGF_DELETE) {
		/*
		 * Message terminating transaction, destroy the related
		 * state, the original message, and this message (if it
		 * isn't the original message due to a CREATE|DELETE).
		 */
		pthread_mutex_lock(&iocom->mtx);
		state->rxcmd |= HAMMER2_MSGF_DELETE;
		if (state->txcmd & HAMMER2_MSGF_DELETE) {
			if (state->msg == msg)
				state->msg = NULL;
			assert(state->flags & HAMMER2_STATE_INSERTED);
			if (state->rxcmd & HAMMER2_MSGF_REPLY) {
				assert(msg->any.head.cmd & HAMMER2_MSGF_REPLY);
				RB_REMOVE(hammer2_state_tree,
					  &iocom->router.statewr_tree, state);
			} else {
				assert((msg->any.head.cmd & HAMMER2_MSGF_REPLY) == 0);
				RB_REMOVE(hammer2_state_tree,
					  &iocom->router.staterd_tree, state);
			}
			state->flags &= ~HAMMER2_STATE_INSERTED;
			hammer2_state_free(state);
		} else {
			;
		}
		pthread_mutex_unlock(&iocom->mtx);
		hammer2_msg_free(msg);
	} else if (state->msg != msg) {
		/*
		 * Message not terminating transaction, leave state intact
		 * and free message if it isn't the CREATE message.
		 */
		hammer2_msg_free(msg);
	}
}

static void
hammer2_state_cleanuptx(hammer2_iocom_t *iocom, hammer2_msg_t *msg)
{
	hammer2_state_t *state;

	if ((state = msg->state) == NULL) {
		hammer2_msg_free(msg);
	} else if (msg->any.head.cmd & HAMMER2_MSGF_DELETE) {
		pthread_mutex_lock(&iocom->mtx);
		state->txcmd |= HAMMER2_MSGF_DELETE;
		if (state->rxcmd & HAMMER2_MSGF_DELETE) {
			if (state->msg == msg)
				state->msg = NULL;
			assert(state->flags & HAMMER2_STATE_INSERTED);
			if (state->txcmd & HAMMER2_MSGF_REPLY) {
				assert(msg->any.head.cmd & HAMMER2_MSGF_REPLY);
				RB_REMOVE(hammer2_state_tree,
					  &iocom->router.staterd_tree, state);
			} else {
				assert((msg->any.head.cmd & HAMMER2_MSGF_REPLY) == 0);
				RB_REMOVE(hammer2_state_tree,
					  &iocom->router.statewr_tree, state);
			}
			state->flags &= ~HAMMER2_STATE_INSERTED;
			hammer2_state_free(state);
		} else {
			;
		}
		pthread_mutex_unlock(&iocom->mtx);
		hammer2_msg_free(msg);
	} else if (state->msg != msg) {
		hammer2_msg_free(msg);
	}
}

/*
 * Called with iocom locked
 */
void
hammer2_state_free(hammer2_state_t *state)
{
	hammer2_iocom_t *iocom = state->iocom;
	hammer2_msg_t *msg;
	char dummy;

	if (DebugOpt) {
		fprintf(stderr, "terminate state %p id=%08x\n",
			state, (uint32_t)state->msgid);
	}
	assert(state->any.any == NULL);
	msg = state->msg;
	state->msg = NULL;
	if (msg)
		hammer2_msg_free_locked(msg);
	free(state);

	/*
	 * When an iocom error is present we are trying to close down the
	 * iocom, but we have to wait for all states to terminate before
	 * we can do so.  The iocom rx code will terminate the receive side
	 * for all transactions by simulating incoming DELETE messages,
	 * but the state doesn't go away until both sides are terminated.
	 *
	 * We may have to wake up the rx code.
	 */
	if (iocom->ioq_rx.error &&
	    RB_EMPTY(&iocom->router.staterd_tree) &&
	    RB_EMPTY(&iocom->router.statewr_tree)) {
		dummy = 0;
		write(iocom->wakeupfds[1], &dummy, 1);
	}
}

const char *
hammer2_basecmd_str(uint32_t cmd)
{
	static char buf[64];
	char protobuf[32];
	char cmdbuf[32];
	const char *protostr;
	const char *cmdstr;

	switch(cmd & HAMMER2_MSGF_PROTOS) {
	case HAMMER2_MSG_PROTO_LNK:
		protostr = "LNK_";
		break;
	case HAMMER2_MSG_PROTO_DBG:
		protostr = "DBG_";
		break;
	case HAMMER2_MSG_PROTO_DOM:
		protostr = "DOM_";
		break;
	case HAMMER2_MSG_PROTO_CAC:
		protostr = "CAC_";
		break;
	case HAMMER2_MSG_PROTO_QRM:
		protostr = "QRM_";
		break;
	case HAMMER2_MSG_PROTO_BLK:
		protostr = "BLK_";
		break;
	case HAMMER2_MSG_PROTO_VOP:
		protostr = "VOP_";
		break;
	default:
		snprintf(protobuf, sizeof(protobuf), "%x_",
			(cmd & HAMMER2_MSGF_PROTOS) >> 20);
		protostr = protobuf;
		break;
	}

	switch(cmd & (HAMMER2_MSGF_PROTOS |
		      HAMMER2_MSGF_CMDS |
		      HAMMER2_MSGF_SIZE)) {
	case HAMMER2_LNK_PAD:
		cmdstr = "PAD";
		break;
	case HAMMER2_LNK_PING:
		cmdstr = "PING";
		break;
	case HAMMER2_LNK_AUTH:
		cmdstr = "AUTH";
		break;
	case HAMMER2_LNK_CONN:
		cmdstr = "CONN";
		break;
	case HAMMER2_LNK_SPAN:
		cmdstr = "SPAN";
		break;
	case HAMMER2_LNK_ERROR:
		if (cmd & HAMMER2_MSGF_DELETE)
			cmdstr = "RETURN";
		else
			cmdstr = "RESULT";
		break;
	case HAMMER2_DBG_SHELL:
		cmdstr = "SHELL";
		break;
	default:
		snprintf(cmdbuf, sizeof(cmdbuf),
			 "%06x", (cmd & (HAMMER2_MSGF_PROTOS |
					 HAMMER2_MSGF_CMDS |
					 HAMMER2_MSGF_SIZE)));
		cmdstr = cmdbuf;
		break;
	}
	snprintf(buf, sizeof(buf), "%s%s", protostr, cmdstr);
	return (buf);
}

const char *
hammer2_msg_str(hammer2_msg_t *msg)
{
	hammer2_state_t *state;
	static char buf[256];
	char errbuf[16];
	char statebuf[64];
	char flagbuf[64];
	const char *statestr;
	const char *errstr;
	uint32_t basecmd;
	int i;

	/*
	 * Parse the state
	 */
	if ((state = msg->state) != NULL) {
		basecmd = (state->rxcmd & HAMMER2_MSGF_REPLY) ?
			  state->txcmd : state->rxcmd;
		snprintf(statebuf, sizeof(statebuf),
			 " %s=%s,L=%s%s,R=%s%s",
			 ((state->txcmd & HAMMER2_MSGF_REPLY) ?
				"rcvcmd" : "sndcmd"),
			 hammer2_basecmd_str(basecmd),
			 ((state->txcmd & HAMMER2_MSGF_CREATE) ? "C" : ""),
			 ((state->txcmd & HAMMER2_MSGF_DELETE) ? "D" : ""),
			 ((state->rxcmd & HAMMER2_MSGF_CREATE) ? "C" : ""),
			 ((state->rxcmd & HAMMER2_MSGF_DELETE) ? "D" : "")
		);
		statestr = statebuf;
	} else {
		statestr = "";
	}

	/*
	 * Parse the error
	 */
	switch(msg->any.head.error) {
	case 0:
		errstr = "";
		break;
	case HAMMER2_IOQ_ERROR_SYNC:
		errstr = "err=IOQ:NOSYNC";
		break;
	case HAMMER2_IOQ_ERROR_EOF:
		errstr = "err=IOQ:STREAMEOF";
		break;
	case HAMMER2_IOQ_ERROR_SOCK:
		errstr = "err=IOQ:SOCKERR";
		break;
	case HAMMER2_IOQ_ERROR_FIELD:
		errstr = "err=IOQ:BADFIELD";
		break;
	case HAMMER2_IOQ_ERROR_HCRC:
		errstr = "err=IOQ:BADHCRC";
		break;
	case HAMMER2_IOQ_ERROR_XCRC:
		errstr = "err=IOQ:BADXCRC";
		break;
	case HAMMER2_IOQ_ERROR_ACRC:
		errstr = "err=IOQ:BADACRC";
		break;
	case HAMMER2_IOQ_ERROR_STATE:
		errstr = "err=IOQ:BADSTATE";
		break;
	case HAMMER2_IOQ_ERROR_NOPEER:
		errstr = "err=IOQ:PEERCONFIG";
		break;
	case HAMMER2_IOQ_ERROR_NORKEY:
		errstr = "err=IOQ:BADRKEY";
		break;
	case HAMMER2_IOQ_ERROR_NOLKEY:
		errstr = "err=IOQ:BADLKEY";
		break;
	case HAMMER2_IOQ_ERROR_KEYXCHGFAIL:
		errstr = "err=IOQ:BADKEYXCHG";
		break;
	case HAMMER2_IOQ_ERROR_KEYFMT:
		errstr = "err=IOQ:BADFMT";
		break;
	case HAMMER2_IOQ_ERROR_BADURANDOM:
		errstr = "err=IOQ:BADRANDOM";
		break;
	case HAMMER2_IOQ_ERROR_MSGSEQ:
		errstr = "err=IOQ:BADSEQ";
		break;
	case HAMMER2_IOQ_ERROR_EALREADY:
		errstr = "err=IOQ:DUPMSG";
		break;
	case HAMMER2_IOQ_ERROR_TRANS:
		errstr = "err=IOQ:BADTRANS";
		break;
	case HAMMER2_IOQ_ERROR_IVWRAP:
		errstr = "err=IOQ:IVWRAP";
		break;
	case HAMMER2_IOQ_ERROR_MACFAIL:
		errstr = "err=IOQ:MACFAIL";
		break;
	case HAMMER2_IOQ_ERROR_ALGO:
		errstr = "err=IOQ:ALGOFAIL";
		break;
	case HAMMER2_MSG_ERR_NOSUPP:
		errstr = "err=NOSUPPORT";
		break;
	default:
		snprintf(errbuf, sizeof(errbuf),
			 " err=%d", msg->any.head.error);
		errstr = errbuf;
		break;
	}

	/*
	 * Message flags
	 */
	i = 0;
	if (msg->any.head.cmd & (HAMMER2_MSGF_CREATE | HAMMER2_MSGF_DELETE |
				 HAMMER2_MSGF_ABORT | HAMMER2_MSGF_REPLY)) {
		flagbuf[i++] = '|';
		if (msg->any.head.cmd & HAMMER2_MSGF_CREATE)
			flagbuf[i++] = 'C';
		if (msg->any.head.cmd & HAMMER2_MSGF_DELETE)
			flagbuf[i++] = 'D';
		if (msg->any.head.cmd & HAMMER2_MSGF_REPLY)
			flagbuf[i++] = 'R';
		if (msg->any.head.cmd & HAMMER2_MSGF_ABORT)
			flagbuf[i++] = 'A';
	}
	flagbuf[i] = 0;

	/*
	 * Generate the buf
	 */
	snprintf(buf, sizeof(buf),
		"msg=%s%s %s id=%08x span=%08x %s",
		 hammer2_basecmd_str(msg->any.head.cmd),
		 flagbuf,
		 errstr,
		 (uint32_t)(intmax_t)msg->any.head.msgid,   /* for brevity */
		 (uint32_t)(intmax_t)msg->any.head.spanid,  /* for brevity */
		 statestr);

	return(buf);
}
