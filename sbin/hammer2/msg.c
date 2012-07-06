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
static int hammer2_state_msgtx(hammer2_iocom_t *iocom, hammer2_msg_t *msg);
static void hammer2_state_cleanuptx(hammer2_iocom_t *iocom, hammer2_msg_t *msg);

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

void
hammer2_ioq_done(hammer2_iocom_t *iocom __unused, hammer2_ioq_t *ioq)
{
	hammer2_msg_t *msg;

	while ((msg = TAILQ_FIRST(&ioq->msgq)) != NULL) {
		TAILQ_REMOVE(&ioq->msgq, msg, qentry);
		hammer2_msg_free(iocom, msg);
	}
	if ((msg = ioq->msg) != NULL) {
		ioq->msg = NULL;
		hammer2_msg_free(iocom, msg);
	}
}

/*
 * Initialize a low-level communications channel
 */
void
hammer2_iocom_init(hammer2_iocom_t *iocom, int sock_fd, int alt_fd)
{
	bzero(iocom, sizeof(*iocom));

	RB_INIT(&iocom->staterd_tree);
	RB_INIT(&iocom->statewr_tree);
	TAILQ_INIT(&iocom->freeq);
	TAILQ_INIT(&iocom->freeq_aux);
	iocom->sock_fd = sock_fd;
	iocom->alt_fd = alt_fd;
	iocom->flags = HAMMER2_IOCOMF_RREQ | HAMMER2_IOCOMF_WIDLE;
	hammer2_ioq_init(iocom, &iocom->ioq_rx);
	hammer2_ioq_init(iocom, &iocom->ioq_tx);

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

void
hammer2_iocom_done(hammer2_iocom_t *iocom)
{
	hammer2_msg_t *msg;

	iocom->sock_fd = -1;
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
}

/*
 * Allocate a new one-way message.
 */
hammer2_msg_t *
hammer2_msg_alloc(hammer2_iocom_t *iocom, size_t aux_size, uint32_t cmd)
{
	hammer2_msg_t *msg;
	int hbytes;

	if (aux_size) {
		aux_size = (aux_size + HAMMER2_MSG_ALIGNMASK) &
			   ~HAMMER2_MSG_ALIGNMASK;
		if ((msg = TAILQ_FIRST(&iocom->freeq_aux)) != NULL)
			TAILQ_REMOVE(&iocom->freeq_aux, msg, qentry);
	} else {
		if ((msg = TAILQ_FIRST(&iocom->freeq)) != NULL)
			TAILQ_REMOVE(&iocom->freeq, msg, qentry);
	}
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
	msg->any.head.aux_icrc = 0;
	msg->any.head.cmd = cmd;

	return (msg);
}

/*
 * Free a message so it can be reused afresh.
 *
 * NOTE: aux_size can be 0 with a non-NULL aux_data.
 */
void
hammer2_msg_free(hammer2_iocom_t *iocom, hammer2_msg_t *msg)
{
	if (msg->aux_data)
		TAILQ_INSERT_TAIL(&iocom->freeq_aux, msg, qentry);
	else
		TAILQ_INSERT_TAIL(&iocom->freeq, msg, qentry);
}

/*
 * I/O core loop for an iocom.
 */
void
hammer2_iocom_core(hammer2_iocom_t *iocom,
		   void (*recvmsg_func)(hammer2_iocom_t *),
		   void (*sendmsg_func)(hammer2_iocom_t *),
		   void (*altmsg_func)(hammer2_iocom_t *))
{
	struct pollfd fds[2];
	int timeout;

	iocom->recvmsg_callback = recvmsg_func;
	iocom->sendmsg_callback = sendmsg_func;
	iocom->altmsg_callback = altmsg_func;

	while ((iocom->flags & HAMMER2_IOCOMF_EOF) == 0) {
		timeout = 5000;

		fds[0].fd = iocom->sock_fd;
		fds[0].events = 0;
		fds[0].revents = 0;

		if (iocom->flags & HAMMER2_IOCOMF_RREQ)
			fds[0].events |= POLLIN;
		else
			timeout = 0;
		if ((iocom->flags & HAMMER2_IOCOMF_WIDLE) == 0) {
			if (iocom->flags & HAMMER2_IOCOMF_WREQ)
				fds[0].events |= POLLOUT;
			else
				timeout = 0;
		}

		if (iocom->alt_fd >= 0) {
			fds[1].fd = iocom->alt_fd;
			fds[1].events |= POLLIN;
			fds[1].revents = 0;
			poll(fds, 2, timeout);
		} else {
			poll(fds, 1, timeout);
		}
		if ((fds[0].revents & POLLIN) ||
		    (iocom->flags & HAMMER2_IOCOMF_RREQ) == 0) {
			iocom->recvmsg_callback(iocom);
		}
		if ((iocom->flags & HAMMER2_IOCOMF_WIDLE) == 0) {
			if ((fds[0].revents & POLLOUT) ||
			    (iocom->flags & HAMMER2_IOCOMF_WREQ) == 0) {
				iocom->sendmsg_callback(iocom);
			}
		}
		if (iocom->alt_fd >= 0 && (fds[1].revents & POLLIN))
			iocom->altmsg_callback(iocom);
	}
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
	uint16_t xcrc16;
	uint32_t xcrc32;
	int error;

again:
	/*
	 * If a message is already pending we can just remove and
	 * return it.  Message state has already been processed.
	 */
	if ((msg = TAILQ_FIRST(&ioq->msgq)) != NULL) {
		TAILQ_REMOVE(&ioq->msgq, msg, qentry);
		return (msg);
	}

	/*
	 * Message read in-progress (msg is NULL at the moment).  We don't
	 * allocate a msg until we have its core header.
	 */
	bytes = ioq->fifo_end - ioq->fifo_beg;
	nmax = sizeof(ioq->buf) - ioq->fifo_end;
	msg = ioq->msg;

	switch(ioq->state) {
	case HAMMER2_MSGQ_STATE_HEADER1:
		/*
		 * Load the primary header, fail on any non-trivial read
		 * error or on EOF.  Since the primary header is the same
		 * size is the message alignment it will never straddle
		 * the end of the buffer.
		 */
		if (bytes < (int)sizeof(msg->any.head)) {
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
			ioq->fifo_end += n;
			bytes += n;
			nmax -= n;
		}

		/*
		 * Insufficient data accumulated (msg is NULL, caller will
		 * retry on event).
		 */
		assert(msg == NULL);
		if (bytes < (int)sizeof(msg->any.head))
			break;

		/*
		 * Calculate the header, decrypt data received so far.
		 * Data will be decrypted in-place.  Partial blocks are
		 * not immediately decrypted.
		 */
		hammer2_crypto_decrypt(iocom, ioq);
		head = (void *)(ioq->buf + ioq->fifo_beg);

		/*
		 * Check and fixup the core header.  Note that the icrc
		 * has to be calculated before any fixups, but the crc
		 * fields in the msg may have to be swapped like everything
		 * else.
		 */
		if (head->magic != HAMMER2_MSGHDR_MAGIC &&
		    head->magic != HAMMER2_MSGHDR_MAGIC_REV) {
			ioq->error = HAMMER2_IOQ_ERROR_SYNC;
			break;
		}

		xcrc32 = hammer2_icrc32((char *)head + HAMMER2_MSGHDR_CRCOFF,
					HAMMER2_MSGHDR_CRCBYTES);
		if (head->magic == HAMMER2_MSGHDR_MAGIC_REV) {
			hammer2_bswap_head(head);
		}
		xcrc16 = (uint16_t)xcrc32 ^ (uint16_t)(xcrc32 >> 16);
		if (xcrc16 != head->icrc1) {
			ioq->error = HAMMER2_IOQ_ERROR_HCRC;
			break;
		}

		/*
		 * Calculate the full header size and aux data size
		 */
		ioq->hbytes = (head->cmd & HAMMER2_MSGF_SIZE) *
			      HAMMER2_MSG_ALIGN;
		ioq->abytes = head->aux_bytes * HAMMER2_MSG_ALIGN;
		if (ioq->hbytes < sizeof(msg->any.head) ||
		    ioq->hbytes > sizeof(msg->any) ||
		    ioq->abytes > HAMMER2_MSGAUX_MAX) {
			ioq->error = HAMMER2_IOQ_ERROR_FIELD;
			break;
		}

		/*
		 * Finally allocate the message and copy the core header
		 * to the embedded extended header.
		 *
		 * Initialize msg->aux_size to 0 and use it to track
		 * the amount of data copied from the stream.
		 */
		msg = hammer2_msg_alloc(iocom, ioq->abytes, 0);
		ioq->msg = msg;

		/*
		 * We are either done or we fall-through
		 */
		if (ioq->hbytes == sizeof(msg->any.head) && ioq->abytes == 0) {
			bcopy(head, &msg->any.head, sizeof(msg->any.head));
			ioq->fifo_beg += ioq->hbytes;
			break;
		}

		/*
		 * Fall through to the next state.  Make sure that the
		 * extended header does not straddle the end of the buffer.
		 * We still want to issue larger reads into our buffer,
		 * book-keeping is easier if we don't bcopy() yet.
		 */
		if (bytes + nmax < ioq->hbytes) {
			bcopy(ioq->buf + ioq->fifo_beg, ioq->buf, bytes);
			ioq->fifo_cdx -= ioq->fifo_beg;
			ioq->fifo_beg = 0;
			ioq->fifo_end = bytes;
			nmax = sizeof(ioq->buf) - ioq->fifo_end;
		}
		ioq->state = HAMMER2_MSGQ_STATE_HEADER2;
		/* fall through */
	case HAMMER2_MSGQ_STATE_HEADER2:
		/*
		 * Fill out the extended header.
		 */
		assert(msg != NULL);
		if (bytes < ioq->hbytes) {
			n = read(iocom->sock_fd,
				 msg->any.buf + ioq->fifo_end,
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
			ioq->fifo_end += n;
			bytes += n;
			nmax -= n;
		}

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
		 * so far.
		 */
		hammer2_crypto_decrypt(iocom, ioq);
		head = (void *)(ioq->buf + ioq->fifo_beg);

		/*
		 * Check the crc on the extended header
		 */
		if (ioq->hbytes > sizeof(hammer2_msg_hdr_t)) {
			xcrc32 = hammer2_icrc32(head + 1,
						ioq->hbytes - sizeof(*head));
			xcrc16 = (uint16_t)xcrc32 ^ (uint16_t)(xcrc32 >> 16);
			if (head->icrc2 != xcrc16) {
				ioq->error = HAMMER2_IOQ_ERROR_XCRC;
				break;
			}
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
		 * Must adjust nmax and bytes (and the state) when falling
		 * through.
		 */
		ioq->fifo_beg += ioq->hbytes;
		nmax -= ioq->hbytes;
		bytes -= ioq->hbytes;
		ioq->state = HAMMER2_MSGQ_STATE_AUXDATA1;
		/* fall through */
	case HAMMER2_MSGQ_STATE_AUXDATA1:
		/*
		 * Copy the partial or complete payload from remaining
		 * bytes in the FIFO.  We have to fall-through either
		 * way so we can check the crc.
		 *
		 * Adjust msg->aux_size to the final actual value.
		 */
		ioq->already = ioq->fifo_cdx - ioq->fifo_beg;
		if (ioq->already > ioq->abytes)
			ioq->already = ioq->abytes;
		if (bytes >= ioq->abytes) {
			bcopy(ioq->buf + ioq->fifo_beg, msg->aux_data,
			      ioq->abytes);
			msg->aux_size = ioq->abytes;
			ioq->fifo_beg += ioq->abytes;
			if (ioq->fifo_cdx < ioq->fifo_beg)
				ioq->fifo_cdx = ioq->fifo_beg;
			bytes -= ioq->abytes;
		} else if (bytes) {
			bcopy(ioq->buf + ioq->fifo_beg, msg->aux_data,
			      bytes);
			msg->aux_size = bytes;
			ioq->fifo_beg += bytes;
			if (ioq->fifo_cdx < ioq->fifo_beg)
				ioq->fifo_cdx = ioq->fifo_beg;
			bytes = 0;
		} else {
			msg->aux_size = 0;
		}
		ioq->state = HAMMER2_MSGQ_STATE_AUXDATA2;
		/* fall through */
	case HAMMER2_MSGQ_STATE_AUXDATA2:
		/*
		 * Read the remainder of the payload directly into the
		 * msg->aux_data buffer.
		 */
		assert(msg);
		if (msg->aux_size < ioq->abytes) {
			assert(bytes == 0);
			n = read(iocom->sock_fd,
				 msg->aux_data + msg->aux_size,
				 ioq->abytes - msg->aux_size);
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
			msg->aux_size += n;
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
		hammer2_crypto_decrypt_aux(iocom, ioq, msg, ioq->already);

		/*
		 * Check aux_icrc, then we are done.
		 */
		xcrc32 = hammer2_icrc32(msg->aux_data, msg->aux_size);
		if (xcrc32 != msg->any.head.aux_icrc) {
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
				hammer2_msg_free(iocom, msg);
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
			hammer2_msg_free(iocom, msg);
			ioq->msg = NULL;
		}

		/*
		 * No more I/O read processing
		 */
		ioq->state = HAMMER2_MSGQ_STATE_ERROR;

		/*
		 * Return LNK_ERROR for any open transaction, and finally
		 * as a non-transactional message when no transactions are
		 * left.
		 */
		msg = hammer2_msg_alloc(iocom, 0, 0);
		bzero(&msg->any.head, sizeof(msg->any.head));
		msg->any.head.magic = HAMMER2_MSGHDR_MAGIC;
		msg->any.head.cmd = HAMMER2_LNK_ERROR;
		msg->any.head.error = ioq->error;

		if ((state = RB_ROOT(&iocom->staterd_tree)) != NULL) {
			/*
			 * Active transactions are still present.  Simulate
			 * the other end sending us a DELETE.
			 */
			state->txcmd |= HAMMER2_MSGF_DELETE;
			msg->state = state;
			msg->any.head.source = state->source;
			msg->any.head.target = state->target;
			msg->any.head.cmd |= HAMMER2_MSGF_ABORT |
					     HAMMER2_MSGF_DELETE;
		} else {
			/*
			 * No active transactions remain
			 */
			msg->state = NULL;
			iocom->flags |= HAMMER2_IOCOMF_EOF;
		}
	} else if (msg == NULL) {
		/*
		 * Insufficient data received to finish building the message,
		 * set RREQ and return NULL.
		 *
		 * Leave ioq->msg intact.
		 * Leave the FIFO intact.
		 */
		iocom->flags |= HAMMER2_IOCOMF_RREQ;
#if 0
		ioq->fifo_cdx = 0;
		ioq->fifo_beg = 0;
		ioq->fifo_end = 0;
#endif
	} else {
		/*
		 * Return msg, clear the FIFO if it is now empty.
		 * Flag RREQ if the caller needs to wait for a read-event
		 * or not.
		 *
		 * The fifo has already been advanced past the message.
		 * Trivially reset the FIFO indices if possible.
		 */
		if (ioq->fifo_beg == ioq->fifo_end) {
			iocom->flags |= HAMMER2_IOCOMF_RREQ;
			ioq->fifo_cdx = 0;
			ioq->fifo_beg = 0;
			ioq->fifo_end = 0;
		} else {
			iocom->flags &= ~HAMMER2_IOCOMF_RREQ;
		}
		ioq->state = HAMMER2_MSGQ_STATE_HEADER1;
		ioq->msg = NULL;
	}
	return (msg);
}

/*
 * Calculate the header and data crc's and write a low-level message to
 * the connection.  If aux_icrc is non-zero the aux_data crc is already
 * assumed to have been set.
 *
 * A non-NULL msg is added to the queue but not necessarily flushed.
 * Calling this function with msg == NULL will get a flush going.
 */
void
hammer2_ioq_write(hammer2_iocom_t *iocom, hammer2_msg_t *msg)
{
	hammer2_ioq_t *ioq = &iocom->ioq_tx;
	uint16_t xcrc16;
	uint32_t xcrc32;
	int hbytes;
	int error;

	assert(msg);

	/*
	 * Process transactional state.
	 */
	if (ioq->error == 0) {
		error = hammer2_state_msgtx(iocom, msg);
		if (error) {
			if (error == HAMMER2_IOQ_ERROR_EALREADY) {
				hammer2_msg_free(iocom, msg);
			} else {
				ioq->error = error;
			}
		}
	}

	/*
	 * Process terminal connection errors.
	 */
	if (ioq->error) {
		TAILQ_INSERT_TAIL(&ioq->msgq, msg, qentry);
		++ioq->msgcount;
		hammer2_iocom_drain(iocom);
		return;
	}

	/*
	 * Finish populating the msg fields.  The salt ensures that the iv[]
	 * array is ridiculously randomized and we also re-seed our PRNG
	 * every 32768 messages just to be sure.
	 */
	msg->any.head.magic = HAMMER2_MSGHDR_MAGIC;
	msg->any.head.salt = (random() << 8) | (ioq->seq & 255);
	++ioq->seq;
	if ((ioq->seq & 32767) == 0)
		srandomdev();

	/*
	 * Calculate aux_icrc if 0, calculate icrc2, and finally
	 * calculate icrc1.
	 */
	if (msg->aux_size && msg->any.head.aux_icrc == 0) {
		assert((msg->aux_size & HAMMER2_MSG_ALIGNMASK) == 0);
		xcrc32 = hammer2_icrc32(msg->aux_data, msg->aux_size);
		msg->any.head.aux_icrc = xcrc32;
	}
	msg->any.head.aux_bytes = msg->aux_size / HAMMER2_MSG_ALIGN;
	assert((msg->aux_size & HAMMER2_MSG_ALIGNMASK) == 0);

	if ((msg->any.head.cmd & HAMMER2_MSGF_SIZE) >
	    sizeof(msg->any.head) / HAMMER2_MSG_ALIGN) {
		hbytes = (msg->any.head.cmd & HAMMER2_MSGF_SIZE) *
			HAMMER2_MSG_ALIGN;
		hbytes -= sizeof(msg->any.head);
		xcrc32 = hammer2_icrc32(&msg->any.head + 1, hbytes);
		xcrc16 = (uint16_t)xcrc32 ^ (uint16_t)(xcrc32 >> 16);
		msg->any.head.icrc2 = xcrc16;
	} else {
		msg->any.head.icrc2 = 0;
	}
	xcrc32 = hammer2_icrc32(msg->any.buf + HAMMER2_MSGHDR_CRCOFF,
				HAMMER2_MSGHDR_CRCBYTES);
	xcrc16 = (uint16_t)xcrc32 ^ (uint16_t)(xcrc32 >> 16);
	msg->any.head.icrc1 = xcrc16;

	/*
	 * XXX Encrypt the message
	 */

	/*
	 * Enqueue the message.
	 */
	TAILQ_INSERT_TAIL(&ioq->msgq, msg, qentry);
	++ioq->msgcount;
	iocom->flags &= ~HAMMER2_IOCOMF_WIDLE;

	/*
	 * Flush if we know we can write (WREQ not set) and if
	 * sufficient messages have accumulated.  Otherwise hold
	 * off to avoid piecemeal system calls.
	 */
	if (iocom->flags & HAMMER2_IOCOMF_WREQ)
		return;
	if (ioq->msgcount < HAMMER2_IOQ_MAXIOVEC / 2)
		return;
	hammer2_iocom_flush(iocom);
}

void
hammer2_iocom_flush(hammer2_iocom_t *iocom)
{
	hammer2_ioq_t *ioq = &iocom->ioq_tx;
	hammer2_msg_t *msg;
	ssize_t nmax;
	ssize_t nact;
	struct iovec iov[HAMMER2_IOQ_MAXIOVEC];
	size_t hbytes;
	size_t abytes;
	int hoff;
	int aoff;
	int n;

	/*
	 * Pump messages out the connection by building an iovec.
	 */
	n = 0;
	nmax = 0;

	TAILQ_FOREACH(msg, &ioq->msgq, qentry) {
		hoff = 0;
		hbytes = (msg->any.head.cmd & HAMMER2_MSGF_SIZE) *
			 HAMMER2_MSG_ALIGN;
		aoff = 0;
		abytes = msg->aux_size;
		if (n == 0) {
			hoff += ioq->hbytes;
			aoff += ioq->abytes;
		}
		if (hbytes - hoff > 0) {
			iov[n].iov_base = (char *)&msg->any.head + hoff;
			iov[n].iov_len = hbytes - hoff;
			nmax += hbytes - hoff;
			++n;
			if (n == HAMMER2_IOQ_MAXIOVEC)
				break;
		}
		if (abytes - aoff > 0) {
			assert(msg->aux_data != NULL);
			iov[n].iov_base = msg->aux_data + aoff;
			iov[n].iov_len = abytes - aoff;
			nmax += abytes - aoff;
			++n;
			if (n == HAMMER2_IOQ_MAXIOVEC)
				break;
		}
	}
	if (n == 0)
		return;

	/*
	 * Encrypt and write the data.  The crypto code will move the
	 * data into the fifo and adjust the iov as necessary.  If
	 * encryption is disabled the iov is left alone.
	 *
	 * hammer2_crypto_encrypt_wrote()
	 */
	n = hammer2_crypto_encrypt(iocom, ioq, iov, n);

	/*
	 * Execute the writev() then figure out what happened.
	 */
	nact = writev(iocom->sock_fd, iov, n);
	if (nact < 0) {
		if (errno != EINTR &&
		    errno != EINPROGRESS &&
		    errno != EAGAIN) {
			ioq->error = HAMMER2_IOQ_ERROR_SOCK;
			hammer2_iocom_drain(iocom);
		} else {
			iocom->flags |= HAMMER2_IOCOMF_WREQ;
		}
		return;
	}
	hammer2_crypto_encrypt_wrote(iocom, ioq, nact);
	if (nact == nmax)
		iocom->flags &= ~HAMMER2_IOCOMF_WREQ;
	else
		iocom->flags |= HAMMER2_IOCOMF_WREQ;

	while ((msg = TAILQ_FIRST(&ioq->msgq)) != NULL) {
		hbytes = (msg->any.head.cmd & HAMMER2_MSGF_SIZE) *
			 HAMMER2_MSG_ALIGN;
		abytes = msg->aux_size;

		if ((size_t)nact < hbytes - ioq->hbytes) {
			ioq->hbytes += nact;
			break;
		}
		nact -= hbytes - ioq->hbytes;
		ioq->hbytes = hbytes;
		if ((size_t)nact < abytes - ioq->abytes) {
			ioq->abytes += nact;
			break;
		}
		nact -= abytes - ioq->abytes;

		TAILQ_REMOVE(&ioq->msgq, msg, qentry);
		--ioq->msgcount;
		ioq->hbytes = 0;
		ioq->abytes = 0;

		hammer2_state_cleanuptx(iocom, msg);
	}
	if (msg == NULL) {
		iocom->flags |= HAMMER2_IOCOMF_WIDLE;
		iocom->flags &= ~HAMMER2_IOCOMF_WREQ;
	}
	if (ioq->error) {
		iocom->flags |= HAMMER2_IOCOMF_EOF |
				HAMMER2_IOCOMF_WIDLE;
		iocom->flags &= ~HAMMER2_IOCOMF_WREQ;
	}
}

/*
 * Kill pending msgs on ioq_tx and adjust the flags such that no more
 * write events will occur.  We don't kill read msgs because we want
 * the caller to pull off our contrived terminal error msg to detect
 * the connection failure.
 */
void
hammer2_iocom_drain(hammer2_iocom_t *iocom)
{
	hammer2_ioq_t *ioq = &iocom->ioq_tx;
	hammer2_msg_t *msg;

	while ((msg = TAILQ_FIRST(&ioq->msgq)) != NULL) {
		TAILQ_REMOVE(&ioq->msgq, msg, qentry);
		--ioq->msgcount;
		hammer2_msg_free(iocom, msg);
	}
	iocom->flags |= HAMMER2_IOCOMF_WIDLE;
	iocom->flags &= ~HAMMER2_IOCOMF_WREQ;
}

/*
 * This is a shortcut to formulate a reply to msg with a simple error code.
 * It can reply to transaction or one-way messages, or terminate one side
 * of a stream.  A HAMMER2_LNK_ERROR command code is utilized to encode
 * the error code (which can be 0).
 *
 * Replies to one-way messages are a bit of an oxymoron but the feature
 * is used by the debug (DBG) protocol.
 *
 * The reply contains no data.
 */
void
hammer2_msg_reply(hammer2_iocom_t *iocom, hammer2_msg_t *msg, uint16_t error)
{
	hammer2_msg_t *nmsg;
	uint32_t cmd;

	cmd = HAMMER2_LNK_ERROR;
	if (msg->any.head.cmd & HAMMER2_MSGF_REPLY) {
		/*
		 * Reply to received reply, reply direction uses txcmd.
		 * txcmd will be updated by hammer2_ioq_write().
		 */
		if (msg->state) {
			if ((msg->state->rxcmd & HAMMER2_MSGF_CREATE) == 0)
				cmd |= HAMMER2_MSGF_CREATE;
			cmd |= HAMMER2_MSGF_DELETE;
		}
	} else {
		/*
		 * Reply to received command, reply direction uses rxcmd.
		 * txcmd will be updated by hammer2_ioq_write().
		 */
		cmd |= HAMMER2_MSGF_REPLY;
		if (msg->state) {
			if ((msg->state->rxcmd & HAMMER2_MSGF_CREATE) == 0)
				cmd |= HAMMER2_MSGF_CREATE;
			cmd |= HAMMER2_MSGF_DELETE;
		}
	}
	nmsg = hammer2_msg_alloc(iocom, 0, cmd);
	nmsg->any.head.error = error;
	hammer2_ioq_write(iocom, nmsg);
}

/************************************************************************
 *			TRANSACTION STATE HANDLING			*
 ************************************************************************
 *
 */

RB_GENERATE(hammer2_state_tree, hammer2_state, rbnode, hammer2_state_cmp);

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
	/*lockmgr(&pmp->msglk, LK_EXCLUSIVE);*/

	dummy.msgid = msg->any.head.msgid;
	dummy.source = msg->any.head.source;
	dummy.target = msg->any.head.target;
	iocom_printf(iocom, msg->any.head.cmd,
		     "received msg %08x msgid %u source=%u target=%u\n",
		      msg->any.head.cmd, msg->any.head.msgid,
		      msg->any.head.source, msg->any.head.target);
	if (msg->any.head.cmd & HAMMER2_MSGF_REPLY) {
		state = RB_FIND(hammer2_state_tree,
				&iocom->statewr_tree, &dummy);
	} else {
		state = RB_FIND(hammer2_state_tree,
				&iocom->staterd_tree, &dummy);
	}
	msg->state = state;

	/*
	 * Short-cut one-off or mid-stream messages (state may be NULL).
	 */
	if ((msg->any.head.cmd & (HAMMER2_MSGF_CREATE | HAMMER2_MSGF_DELETE |
				  HAMMER2_MSGF_ABORT)) == 0) {
		/*lockmgr(&pmp->msglk, LK_RELEASE);*/
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
			iocom_printf(iocom, msg->any.head.cmd,
				     "hammer2_state_msgrx: "
				     "duplicate transaction\n");
			error = HAMMER2_IOQ_ERROR_TRANS;
			break;
		}
		state = malloc(sizeof(*state));
		bzero(state, sizeof(*state));
		state->iocom = iocom;
		state->flags = HAMMER2_STATE_DYNAMIC;
		state->msg = msg;
		state->rxcmd = msg->any.head.cmd & ~HAMMER2_MSGF_DELETE;
		RB_INSERT(hammer2_state_tree, &iocom->staterd_tree, state);
		state->flags |= HAMMER2_STATE_INSERTED;
		msg->state = state;
		error = 0;
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
				iocom_printf(iocom, msg->any.head.cmd,
					     "hammer2_state_msgrx: "
					     "no state for DELETE\n");
				error = HAMMER2_IOQ_ERROR_TRANS;
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
				iocom_printf(iocom, msg->any.head.cmd,
					     "hammer2_state_msgrx: "
					     "state reused for DELETE\n");
				error = HAMMER2_IOQ_ERROR_TRANS;
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
			iocom_printf(iocom, msg->any.head.cmd,
				     "hammer2_state_msgrx: "
				     "no state match for REPLY cmd=%08x\n",
				     msg->any.head.cmd);
			error = HAMMER2_IOQ_ERROR_TRANS;
			break;
		}
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
				iocom_printf(iocom, msg->any.head.cmd,
					     "hammer2_state_msgrx: "
					     "no state match for "
					     "REPLY|DELETE\n");
				error = HAMMER2_IOQ_ERROR_TRANS;
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
				iocom_printf(iocom, msg->any.head.cmd,
					     "hammer2_state_msgrx: "
					     "state reused for REPLY|DELETE\n");
				error = HAMMER2_IOQ_ERROR_TRANS;
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
	/*lockmgr(&pmp->msglk, LK_RELEASE);*/
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
		hammer2_msg_free(iocom, msg);
	} else if (msg->any.head.cmd & HAMMER2_MSGF_DELETE) {
		/*
		 * Message terminating transaction, destroy the related
		 * state, the original message, and this message (if it
		 * isn't the original message due to a CREATE|DELETE).
		 */
		/*lockmgr(&pmp->msglk, LK_EXCLUSIVE);*/
		state->rxcmd |= HAMMER2_MSGF_DELETE;
		if (state->txcmd & HAMMER2_MSGF_DELETE) {
			if (state->msg == msg)
				state->msg = NULL;
			assert(state->flags & HAMMER2_STATE_INSERTED);
			if (msg->any.head.cmd & HAMMER2_MSGF_REPLY) {
				RB_REMOVE(hammer2_state_tree,
					  &iocom->statewr_tree, state);
			} else {
				RB_REMOVE(hammer2_state_tree,
					  &iocom->staterd_tree, state);
			}
			state->flags &= ~HAMMER2_STATE_INSERTED;
			/*lockmgr(&pmp->msglk, LK_RELEASE);*/
			hammer2_state_free(state);
		} else {
			/*lockmgr(&pmp->msglk, LK_RELEASE);*/
		}
		hammer2_msg_free(iocom, msg);
	} else if (state->msg != msg) {
		/*
		 * Message not terminating transaction, leave state intact
		 * and free message if it isn't the CREATE message.
		 */
		hammer2_msg_free(iocom, msg);
	}
}

/*
 * Process state tracking for a message prior to transmission.
 *
 * Called with msglk held and the msg dequeued.
 *
 * One-off messages are usually with dummy state and msg->state may be NULL
 * in this situation.
 *
 * New transactions (when CREATE is set) will insert the state.
 *
 * May request that caller discard the message by setting *discardp to 1.
 * A NULL state may be returned in this case.
 */
static int
hammer2_state_msgtx(hammer2_iocom_t *iocom, hammer2_msg_t *msg)
{
	hammer2_state_t *state;
	int error;

	/*
	 * Lock RB tree.  If persistent state is present it will have already
	 * been assigned to msg.
	 */
	/*lockmgr(&pmp->msglk, LK_EXCLUSIVE);*/
	state = msg->state;

	/*
	 * Short-cut one-off or mid-stream messages (state may be NULL).
	 */
	if ((msg->any.head.cmd & (HAMMER2_MSGF_CREATE | HAMMER2_MSGF_DELETE |
				  HAMMER2_MSGF_ABORT)) == 0) {
		/*lockmgr(&pmp->msglk, LK_RELEASE);*/
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
		 * Insert the new persistent message state and mark
		 * half-closed if DELETE is set.  Since this is a new
		 * message it isn't possible to transition into the fully
		 * closed state here.
		 *
		 * XXX state must be assigned and inserted by
		 *     hammer2_msg_write().  txcmd is assigned by us
		 *     on-transmit.
		 */
		assert(state != NULL);
#if 0
		if (state == NULL) {
			state = pmp->freerd_state;
			pmp->freerd_state = NULL;
			msg->state = state;
			state->msg = msg;
			state->msgid = msg->any.head.msgid;
			state->source = msg->any.head.source;
			state->target = msg->any.head.target;
		}
		assert((state->flags & HAMMER2_STATE_INSERTED) == 0);
		if (RB_INSERT(hammer2_state_tree, &pmp->staterd_tree, state)) {
			iocom_printf(iocom, msg->any.head.cmd,
				    "hammer2_state_msgtx: "
				    "duplicate transaction\n");
			error = HAMMER2_IOQ_ERROR_TRANS;
			break;
		}
		state->flags |= HAMMER2_STATE_INSERTED;
#endif
		state->txcmd = msg->any.head.cmd & ~HAMMER2_MSGF_DELETE;
		error = 0;
		break;
	case HAMMER2_MSGF_DELETE:
		/*
		 * Sent ABORT+DELETE in case where msgid has already
		 * been fully closed, ignore the message.
		 */
		if (state == NULL) {
			if (msg->any.head.cmd & HAMMER2_MSGF_ABORT) {
				error = HAMMER2_IOQ_ERROR_EALREADY;
			} else {
				iocom_printf(iocom, msg->any.head.cmd,
					     "hammer2_state_msgtx: "
					     "no state match for DELETE\n");
				error = HAMMER2_IOQ_ERROR_TRANS;
			}
			break;
		}

		/*
		 * Sent ABORT+DELETE in case where msgid has
		 * already been reused for an unrelated message,
		 * ignore the message.
		 */
		if ((state->txcmd & HAMMER2_MSGF_CREATE) == 0) {
			if (msg->any.head.cmd & HAMMER2_MSGF_ABORT) {
				error = HAMMER2_IOQ_ERROR_EALREADY;
			} else {
				iocom_printf(iocom, msg->any.head.cmd,
					     "hammer2_state_msgtx: "
					     "state reused for DELETE\n");
				error = HAMMER2_IOQ_ERROR_TRANS;
			}
			break;
		}
		error = 0;
		break;
	default:
		/*
		 * Check for mid-stream ABORT command sent
		 */
		if (msg->any.head.cmd & HAMMER2_MSGF_ABORT) {
			if (state == NULL ||
			    (state->txcmd & HAMMER2_MSGF_CREATE) == 0) {
				error = HAMMER2_IOQ_ERROR_EALREADY;
				break;
			}
		}
		error = 0;
		break;
	case HAMMER2_MSGF_REPLY | HAMMER2_MSGF_CREATE:
	case HAMMER2_MSGF_REPLY | HAMMER2_MSGF_CREATE | HAMMER2_MSGF_DELETE:
		/*
		 * When transmitting a reply with CREATE set the original
		 * persistent state message should already exist.
		 */
		if (state == NULL) {
			iocom_printf(iocom, msg->any.head.cmd,
				     "hammer2_state_msgtx: no state match "
				     "for REPLY | CREATE\n");
			error = HAMMER2_IOQ_ERROR_TRANS;
			break;
		}
		state->txcmd = msg->any.head.cmd & ~HAMMER2_MSGF_DELETE;
		error = 0;
		break;
	case HAMMER2_MSGF_REPLY | HAMMER2_MSGF_DELETE:
		/*
		 * When transmitting a reply with DELETE set the original
		 * persistent state message should already exist.
		 *
		 * This is very similar to the REPLY|CREATE|* case except
		 * txcmd is already stored, so we just add the DELETE flag.
		 *
		 * Sent REPLY+ABORT+DELETE in case where msgid has
		 * already been fully closed, ignore the message.
		 */
		if (state == NULL) {
			if (msg->any.head.cmd & HAMMER2_MSGF_ABORT) {
				error = HAMMER2_IOQ_ERROR_EALREADY;
			} else {
				iocom_printf(iocom, msg->any.head.cmd,
					     "hammer2_state_msgtx: "
					     "no state match for "
					     "REPLY | DELETE\n");
				error = HAMMER2_IOQ_ERROR_TRANS;
			}
			break;
		}

		/*
		 * Sent REPLY+ABORT+DELETE in case where msgid has already
		 * been reused for an unrelated message, ignore the message.
		 */
		if ((state->txcmd & HAMMER2_MSGF_CREATE) == 0) {
			if (msg->any.head.cmd & HAMMER2_MSGF_ABORT) {
				error = HAMMER2_IOQ_ERROR_EALREADY;
			} else {
				iocom_printf(iocom, msg->any.head.cmd,
					     "hammer2_state_msgtx: "
					     "state reused for "
					     "REPLY | DELETE\n");
				error = HAMMER2_IOQ_ERROR_TRANS;
			}
			break;
		}
		error = 0;
		break;
	case HAMMER2_MSGF_REPLY:
		/*
		 * Check for mid-stream ABORT reply sent.
		 *
		 * One-off REPLY messages are allowed for e.g. status updates.
		 */
		if (msg->any.head.cmd & HAMMER2_MSGF_ABORT) {
			if (state == NULL ||
			    (state->txcmd & HAMMER2_MSGF_CREATE) == 0) {
				error = HAMMER2_IOQ_ERROR_EALREADY;
				break;
			}
		}
		error = 0;
		break;
	}
	/*lockmgr(&pmp->msglk, LK_RELEASE);*/
	return (error);
}

static void
hammer2_state_cleanuptx(hammer2_iocom_t *iocom, hammer2_msg_t *msg)
{
	hammer2_state_t *state;

	if ((state = msg->state) == NULL) {
		hammer2_msg_free(iocom, msg);
	} else if (msg->any.head.cmd & HAMMER2_MSGF_DELETE) {
		/*lockmgr(&pmp->msglk, LK_EXCLUSIVE);*/
		state->txcmd |= HAMMER2_MSGF_DELETE;
		if (state->rxcmd & HAMMER2_MSGF_DELETE) {
			if (state->msg == msg)
				state->msg = NULL;
			assert(state->flags & HAMMER2_STATE_INSERTED);
			if (msg->any.head.cmd & HAMMER2_MSGF_REPLY) {
				RB_REMOVE(hammer2_state_tree,
					  &iocom->staterd_tree, state);
			} else {
				RB_REMOVE(hammer2_state_tree,
					  &iocom->statewr_tree, state);
			}
			state->flags &= ~HAMMER2_STATE_INSERTED;
			/*lockmgr(&pmp->msglk, LK_RELEASE);*/
			hammer2_state_free(state);
		} else {
			/*lockmgr(&pmp->msglk, LK_RELEASE);*/
		}
		hammer2_msg_free(iocom, msg);
	} else if (state->msg != msg) {
		hammer2_msg_free(iocom, msg);
	}
}

void
hammer2_state_free(hammer2_state_t *state)
{
	hammer2_iocom_t *iocom = state->iocom;
	hammer2_msg_t *msg;

	msg = state->msg;
	state->msg = NULL;
	if (msg)
		hammer2_msg_free(iocom, msg);
	free(state);
}

/*
 * Indexed messages are stored in a red-black tree indexed by their
 * msgid.  Only persistent messages are indexed.
 */
int
hammer2_state_cmp(hammer2_state_t *state1, hammer2_state_t *state2)
{
	if (state1->source < state2->source)
		return(-1);
	if (state1->source > state2->source)
		return(1);
	if (state1->target < state2->target)
		return(-1);
	if (state1->target > state2->target)
		return(1);
	if (state1->msgid < state2->msgid)
		return(-1);
	if (state1->msgid > state2->msgid)
		return(1);
	return(0);
}
