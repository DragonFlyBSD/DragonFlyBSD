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
hammer2_ioq_done(hammer2_iocom_t *iocom, hammer2_ioq_t *ioq)
{
	hammer2_msg_t *msg;

	while ((msg = TAILQ_FIRST(&ioq->msgq)) != NULL) {
		TAILQ_REMOVE(&ioq->msgq, msg, entry);
		hammer2_iocom_freemsg(iocom, msg);
	}
	if ((msg = ioq->msg) != NULL) {
		ioq->msg = NULL;
		hammer2_iocom_freemsg(iocom, msg);
	}
}

/*
 * Initialize a low-level communications channel
 */
void
hammer2_iocom_init(hammer2_iocom_t *iocom, int sock_fd, int alt_fd)
{
	bzero(iocom, sizeof(*iocom));

	TAILQ_INIT(&iocom->freeq);
	TAILQ_INIT(&iocom->freeq_aux);
	iocom->sock_fd = sock_fd;
	iocom->alt_fd = alt_fd;
	iocom->flags = HAMMER2_IOCOMF_RREQ | HAMMER2_IOCOMF_WIDLE;
	hammer2_ioq_init(iocom, &iocom->ioq_rx);
	hammer2_ioq_init(iocom, &iocom->ioq_tx);

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
		TAILQ_REMOVE(&iocom->freeq, msg, entry);
		free(msg);
	}
	if ((msg = TAILQ_FIRST(&iocom->freeq_aux)) != NULL) {
		TAILQ_REMOVE(&iocom->freeq_aux, msg, entry);
		free(msg->aux_data);
		msg->aux_data = NULL;
		free(msg);
	}
}

hammer2_msg_t *
hammer2_iocom_allocmsg(hammer2_iocom_t *iocom, uint32_t cmd, int aux_size)
{
	hammer2_msg_t *msg;
	int hbytes;

	if (aux_size) {
		aux_size = (aux_size + HAMMER2_MSG_ALIGNMASK) &
			   ~HAMMER2_MSG_ALIGNMASK;
		if ((msg = TAILQ_FIRST(&iocom->freeq_aux)) != NULL)
			TAILQ_REMOVE(&iocom->freeq_aux, msg, entry);
	} else {
		if ((msg = TAILQ_FIRST(&iocom->freeq)) != NULL)
			TAILQ_REMOVE(&iocom->freeq, msg, entry);
	}
	if (msg == NULL) {
		msg = malloc(sizeof(*msg));
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
	msg->flags = 0;
	hbytes = (cmd & HAMMER2_MSGF_SIZE) * HAMMER2_MSG_ALIGN;
	bzero(&msg->any.head, hbytes);
	msg->any.head.cmd = cmd;

	return (msg);
}

void
hammer2_iocom_reallocmsg(hammer2_iocom_t *iocom __unused, hammer2_msg_t *msg,
			 int aux_size)
{
	aux_size = (aux_size + HAMMER2_MSG_ALIGNMASK) & ~HAMMER2_MSG_ALIGNMASK;
	if (aux_size && msg->aux_size != aux_size) {
		if (msg->aux_data) {
			free(msg->aux_data);
			msg->aux_data = NULL;
		}
		msg->aux_data = malloc(aux_size);
		msg->aux_size = aux_size;
	}
	msg->flags = 0;
}

void
hammer2_iocom_freemsg(hammer2_iocom_t *iocom, hammer2_msg_t *msg)
{
	if (msg->aux_data)
		TAILQ_INSERT_TAIL(&iocom->freeq_aux, msg, entry);
	else
		TAILQ_INSERT_TAIL(&iocom->freeq, msg, entry);
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
	int timeout = 5000;

	iocom->recvmsg_callback = recvmsg_func;
	iocom->sendmsg_callback = sendmsg_func;
	iocom->altmsg_callback = altmsg_func;

	while ((iocom->flags & HAMMER2_IOCOMF_EOF) == 0) {
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
 * be returned (and the caller must not call us again after that).
 */
hammer2_msg_t *
hammer2_ioq_read(hammer2_iocom_t *iocom)
{
	hammer2_ioq_t *ioq = &iocom->ioq_rx;
	hammer2_msg_t *msg;
	hammer2_msg_hdr_t *head;
	ssize_t n;
	int bytes;
	int flags;
	int nmax;
	uint16_t xcrc16;
	uint32_t xcrc32;

	/*
	 * If a message is already pending we can just remove and
	 * return it.
	 */
	if ((msg = TAILQ_FIRST(&ioq->msgq)) != NULL) {
		TAILQ_REMOVE(&ioq->msgq, msg, entry);
		return(msg);
	}

	/*
	 * Message read in-progress (msg is NULL at the moment).  We don't
	 * allocate a msg until we have its core header.
	 */
	bytes = ioq->fifo_end - ioq->fifo_beg;
	nmax = sizeof(iocom->rxbuf) - ioq->fifo_end;
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
				 iocom->rxbuf + ioq->fifo_end,
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

		flags = 0;
		head = (void *)(iocom->rxbuf + ioq->fifo_beg);

		/*
		 * XXX Decrypt the core header
		 */

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
			flags |= HAMMER2_MSGX_BSWAPPED;
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
		if (ioq->hbytes < (int)sizeof(msg->any.head) ||
		    ioq->hbytes > (int)sizeof(msg->any) ||
		    ioq->abytes > HAMMER2_MSGAUX_MAX) {
			ioq->error = HAMMER2_IOQ_ERROR_FIELD;
			break;
		}

		/*
		 * Finally allocate the message and copy the core header
		 * to the embedded extended header.
		 */
		if (ioq->abytes) {
			if ((msg = TAILQ_FIRST(&iocom->freeq_aux)) != NULL) {
				TAILQ_REMOVE(&iocom->freeq_aux, msg, entry);
			} else {
				msg = malloc(sizeof(*msg));
				msg->aux_data = NULL;
				msg->aux_size = 0;
			}
			if (msg->aux_size != ioq->abytes) {
				if (msg->aux_data) {
					free(msg->aux_data);
					msg->aux_data = NULL;
				}
				msg->aux_data = malloc(ioq->abytes);
				/* msg->aux_size = ioq->abytes; */
			}
		} else {
			if ((msg = TAILQ_FIRST(&iocom->freeq)) != NULL) {
				TAILQ_REMOVE(&iocom->freeq, msg, entry);
			} else {
				msg = malloc(sizeof(*msg));
				msg->aux_data = NULL;
				/* msg->aux_size = 0; */
			}
		}
		msg->aux_size = 0;	/* data copied so far */
		msg->flags = flags;
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
			bcopy(iocom->rxbuf + ioq->fifo_beg, iocom->rxbuf,
			      bytes);
			ioq->fifo_beg = 0;
			ioq->fifo_end = bytes;
			nmax = sizeof(iocom->rxbuf) - ioq->fifo_end;
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
		 * XXX Decrypt the extended header
		 */
		head = (void *)(iocom->rxbuf + ioq->fifo_beg);

		/*
		 * Check the crc on the extended header
		 */
		if (ioq->hbytes > (int)sizeof(hammer2_msg_hdr_t)) {
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
		 */
		assert(msg->aux_size == 0);
		if (bytes >= ioq->abytes) {
			bcopy(iocom->rxbuf + ioq->fifo_beg, msg->aux_data,
			      ioq->abytes);
			msg->aux_size = ioq->abytes;
			ioq->fifo_beg += ioq->abytes;
			bytes -= ioq->abytes;
		} else if (bytes) {
			bcopy(iocom->rxbuf + ioq->fifo_beg, msg->aux_data,
			      bytes);
			msg->aux_size = bytes;
			ioq->fifo_beg += bytes;
			bytes = 0;
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

		/*
		 * XXX Decrypt the data
		 */

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
	default:
		/*
		 * We don't double-return errors, the caller should not
		 * have called us again after getting an error msg.
		 */
		assert(0);
		break;
	}

	/*
	 * Handle error, RREQ, or completion
	 *
	 * NOTE: nmax and bytes are invalid at this point, we don't bother
	 *	 to update them when breaking out.
	 */
	if (ioq->error) {
		/*
		 * An unrecoverable error occured during processing,
		 * return a special error message.  Try to leave the
		 * ioq state alone for post-mortem debugging.
		 *
		 * Link error messages are returned as one-way messages,
		 * so no flags get set.  Source and target is 0 (link-level),
		 * msgid is 0 (link-level).  All we really need to do is
		 * set up magic, cmd, and error.
		 */
		if (msg == NULL) {
			if ((msg = TAILQ_FIRST(&iocom->freeq)) != NULL) {
				TAILQ_REMOVE(&iocom->freeq, msg, entry);
			} else {
				msg = malloc(sizeof(*msg));
				msg->aux_data = NULL;
				msg->aux_size = 0;
			}
			assert(ioq->msg == NULL);
		} else {
			assert(ioq->msg == msg);
			ioq->msg = NULL;
		}
		if (msg->aux_data) {
			free(msg->aux_data);
			msg->aux_data = NULL;
			msg->aux_size = 0;
		}
		bzero(&msg->any.head, sizeof(msg->any.head));
		msg->any.head.magic = HAMMER2_MSGHDR_MAGIC;
		msg->any.head.cmd = HAMMER2_LNK_ERROR;
		msg->any.head.error = ioq->error;
		ioq->state = HAMMER2_MSGQ_STATE_ERROR;
		iocom->flags |= HAMMER2_IOCOMF_EOF;
	} else if (msg == NULL) {
		/*
		 * Insufficient data received to finish building the message,
		 * set RREQ and return NULL.
		 *
		 * Leave ioq->msg intact.
		 * Leave the FIFO intact.
		 */
		iocom->flags |= HAMMER2_IOCOMF_RREQ;
		ioq->fifo_beg = 0;
		ioq->fifo_end = 0;
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
	ssize_t nmax;
	ssize_t nact;
	int hbytes;
	int abytes;
	int hoff;
	int aoff;
	uint16_t xcrc16;
	uint32_t xcrc32;
	struct iovec iov[HAMMER2_IOQ_MAXIOVEC];
	int n;

	if (ioq->error) {
		if (msg) {
			TAILQ_INSERT_TAIL(&ioq->msgq, msg, entry);
			++ioq->msgcount;
		}
		hammer2_ioq_write_drain(iocom);
		return;
	}

	if (msg) {
		/*
		 * Finish populating the msg fields
		 */
		msg->any.head.magic = HAMMER2_MSGHDR_MAGIC;
		msg->any.head.salt = (random() << 8) | (ioq->seq & 255);
		++ioq->seq;

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
		 * Enqueue the message, stop now if we already know that
		 * we can't write.
		 */
		TAILQ_INSERT_TAIL(&ioq->msgq, msg, entry);
		++ioq->msgcount;
		iocom->flags &= ~HAMMER2_IOCOMF_WIDLE;
		if (iocom->flags & HAMMER2_IOCOMF_WREQ)
			return;

		/*
		 * Flush if we can aggregate several msgs, otherwise
		 * we will wait for the global flush (msg == NULL).
		 */
		if (ioq->msgcount < HAMMER2_IOQ_MAXIOVEC / 2)
			return;
	} else if (iocom->flags &= HAMMER2_IOCOMF_WIDLE) {
		/*
		 * Nothing to do if WIDLE is set.
		 */
		assert(TAILQ_FIRST(&ioq->msgq) == NULL);
		return;
	}

	/*
	 * Pump messages out the connection by building an iovec.
	 */
	n = 0;
	nmax = 0;

	TAILQ_FOREACH(msg, &ioq->msgq, entry) {
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
	 * Execute the writev() then figure out what happened.
	 */
	nact = writev(iocom->sock_fd, iov, n);
	if (nact < 0) {
		if (errno != EINTR &&
		    errno != EINPROGRESS &&
		    errno != EAGAIN) {
			ioq->error = HAMMER2_IOQ_ERROR_SOCK;
			hammer2_ioq_write_drain(iocom);
		} else {
			iocom->flags |= HAMMER2_IOCOMF_WREQ;
		}
		return;
	}
	if (nact == nmax)
		iocom->flags &= ~HAMMER2_IOCOMF_WREQ;
	else
		iocom->flags |= HAMMER2_IOCOMF_WREQ;

	while ((msg = TAILQ_FIRST(&ioq->msgq)) != NULL) {
		hbytes = (msg->any.head.cmd & HAMMER2_MSGF_SIZE) *
			 HAMMER2_MSG_ALIGN;
		abytes = msg->aux_size;

		if (nact < hbytes - ioq->hbytes) {
			ioq->hbytes += nact;
			break;
		}
		nact -= hbytes - ioq->hbytes;
		ioq->hbytes = hbytes;
		if (nact < abytes - ioq->abytes) {
			ioq->abytes += nact;
			break;
		}
		nact -= abytes - ioq->abytes;

		TAILQ_REMOVE(&ioq->msgq, msg, entry);
		--ioq->msgcount;
		ioq->hbytes = 0;
		ioq->abytes = 0;
		if (msg->aux_data)
			TAILQ_INSERT_TAIL(&iocom->freeq_aux, msg, entry);
		else
			TAILQ_INSERT_TAIL(&iocom->freeq, msg, entry);
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
hammer2_ioq_write_drain(hammer2_iocom_t *iocom)
{
	hammer2_ioq_t *ioq = &iocom->ioq_tx;
	hammer2_msg_t *msg;

	while ((msg = TAILQ_FIRST(&ioq->msgq)) != NULL) {
		TAILQ_REMOVE(&ioq->msgq, msg, entry);
		--ioq->msgcount;
		hammer2_iocom_freemsg(iocom, msg);
	}
	iocom->flags |= HAMMER2_IOCOMF_WIDLE;
	iocom->flags &= ~HAMMER2_IOCOMF_WREQ;
}

/*
 * Reply to a message after setting various fields appropriately.
 * This function will swap (source) and (target) and enqueue the
 * message for transmission.
 */
void
hammer2_ioq_reply(hammer2_iocom_t *iocom, hammer2_msg_t *msg)
{
	uint16_t t16;

	t16 = msg->any.head.source;
	msg->any.head.source = msg->any.head.target;
	msg->any.head.target = t16;
	msg->any.head.cmd ^= HAMMER2_MSGF_REPLY;
	hammer2_ioq_write(iocom, msg);
}

void
hammer2_ioq_reply_term(hammer2_iocom_t *iocom, hammer2_msg_t *msg,
		       uint16_t error)
{
	if (msg->any.head.cmd & HAMMER2_MSGF_CREATE) {
		msg->any.head.cmd |= HAMMER2_MSGF_CREATE | HAMMER2_MSGF_DELETE;
		msg->any.head.error = error;
		hammer2_ioq_reply(iocom, msg);
	}
}
