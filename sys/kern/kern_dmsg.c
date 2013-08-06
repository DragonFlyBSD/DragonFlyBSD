/*-
 * Copyright (c) 2012 The DragonFly Project.  All rights reserved.
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
 * TODO: txcmd CREATE state is deferred by txmsgq, need to calculate
 *	 a streaming response.  See subr_diskiocom()'s diskiodone().
 */
#include <sys/param.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/systm.h>
#include <sys/queue.h>
#include <sys/tree.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/thread.h>
#include <sys/globaldata.h>
#include <sys/limits.h>

#include <sys/dmsg.h>

RB_GENERATE(kdmsg_state_tree, kdmsg_state, rbnode, kdmsg_state_cmp);
RB_GENERATE(kdmsg_circuit_tree, kdmsg_circuit, rbnode, kdmsg_circuit_cmp);

static int kdmsg_msg_receive_handling(kdmsg_msg_t *msg);
static int kdmsg_circ_msgrx(kdmsg_msg_t *msg);
static int kdmsg_state_msgrx(kdmsg_msg_t *msg);
static int kdmsg_state_msgtx(kdmsg_msg_t *msg);
static void kdmsg_state_cleanuprx(kdmsg_msg_t *msg);
static void kdmsg_state_cleanuptx(kdmsg_msg_t *msg);
static void kdmsg_state_abort(kdmsg_state_t *state);
static void kdmsg_state_free(kdmsg_state_t *state);

static void kdmsg_iocom_thread_rd(void *arg);
static void kdmsg_iocom_thread_wr(void *arg);
static int kdmsg_autorxmsg(kdmsg_msg_t *msg);
static void kdmsg_autocirc(kdmsg_msg_t *msg);
static int kdmsg_autocirc_reply(kdmsg_state_t *state, kdmsg_msg_t *msg);

static struct lwkt_token kdmsg_token = LWKT_TOKEN_INITIALIZER(kdmsg_token);

void
kdmsg_circ_hold(kdmsg_circuit_t *circ)
{
	atomic_add_int(&circ->refs, 1);
}

void
kdmsg_circ_drop(kdmsg_circuit_t *circ)
{
	kdmsg_iocom_t *iocom;

	if (atomic_fetchadd_int(&circ->refs, -1) == 1) {
		KKASSERT(circ->span_state == NULL &&
			 circ->circ_state == NULL &&
			 circ->rcirc_state == NULL &&
			 circ->recorded == 0);
		iocom = circ->iocom;
		circ->iocom = NULL;
		kfree(circ, iocom->mmsg);
	}
}


/*
 * Initialize the roll-up communications structure for a network
 * messaging session.  This function does not install the socket.
 */
void
kdmsg_iocom_init(kdmsg_iocom_t *iocom, void *handle, uint32_t flags,
		 struct malloc_type *mmsg,
		 int (*rcvmsg)(kdmsg_msg_t *msg))
{
	bzero(iocom, sizeof(*iocom));
	iocom->handle = handle;
	iocom->mmsg = mmsg;
	iocom->rcvmsg = rcvmsg;
	iocom->flags = flags;
	lockinit(&iocom->msglk, "h2msg", 0, 0);
	TAILQ_INIT(&iocom->msgq);
	RB_INIT(&iocom->circ_tree);
	RB_INIT(&iocom->staterd_tree);
	RB_INIT(&iocom->statewr_tree);
}

/*
 * [Re]connect using the passed file pointer.  The caller must ref the
 * fp for us.  We own that ref now.
 */
void
kdmsg_iocom_reconnect(kdmsg_iocom_t *iocom, struct file *fp,
		      const char *subsysname)
{
	/*
	 * Destroy the current connection
	 */
	lockmgr(&iocom->msglk, LK_EXCLUSIVE);
	atomic_set_int(&iocom->msg_ctl, KDMSG_CLUSTERCTL_KILL);
	while (iocom->msgrd_td || iocom->msgwr_td) {
		wakeup(&iocom->msg_ctl);
		lksleep(iocom, &iocom->msglk, 0, "clstrkl", hz);
	}

	/*
	 * Drop communications descriptor
	 */
	if (iocom->msg_fp) {
		fdrop(iocom->msg_fp);
		iocom->msg_fp = NULL;
	}

	/*
	 * Setup new communications descriptor
	 */
	iocom->msg_ctl = 0;
	iocom->msg_fp = fp;
	iocom->msg_seq = 0;
	iocom->flags &= ~KDMSG_IOCOMF_EXITNOACC;

	lwkt_create(kdmsg_iocom_thread_rd, iocom, &iocom->msgrd_td,
		    NULL, 0, -1, "%s-msgrd", subsysname);
	lwkt_create(kdmsg_iocom_thread_wr, iocom, &iocom->msgwr_td,
		    NULL, 0, -1, "%s-msgwr", subsysname);
	lockmgr(&iocom->msglk, LK_RELEASE);
}

/*
 * Caller sets up iocom->auto_lnk_conn and iocom->auto_lnk_span, then calls
 * this function to handle the state machine for LNK_CONN and LNK_SPAN.
 *
 * NOTE: Caller typically also sets the IOCOMF_AUTOCONN, IOCOMF_AUTOSPAN,
 *	 and IOCOMF_AUTOCIRC in the kdmsg_iocom_init() call.  Clients
 *	 typically set IOCOMF_AUTOFORGE to automatically forged circuits
 *	 for received SPANs.
 */
static int kdmsg_lnk_conn_reply(kdmsg_state_t *state, kdmsg_msg_t *msg);
static int kdmsg_lnk_span_reply(kdmsg_state_t *state, kdmsg_msg_t *msg);

void
kdmsg_iocom_autoinitiate(kdmsg_iocom_t *iocom,
			 void (*auto_callback)(kdmsg_msg_t *msg))
{
	kdmsg_msg_t *msg;

	iocom->auto_callback = auto_callback;

	msg = kdmsg_msg_alloc(iocom, NULL,
			      DMSG_LNK_CONN | DMSGF_CREATE,
			      kdmsg_lnk_conn_reply, NULL);
	iocom->auto_lnk_conn.head = msg->any.head;
	msg->any.lnk_conn = iocom->auto_lnk_conn;
	iocom->conn_state = msg->state;
	kdmsg_msg_write(msg);
}

static
int
kdmsg_lnk_conn_reply(kdmsg_state_t *state, kdmsg_msg_t *msg)
{
	kdmsg_iocom_t *iocom = state->iocom;
	kdmsg_msg_t *rmsg;

	if (msg->any.head.cmd & DMSGF_CREATE) {
		rmsg = kdmsg_msg_alloc(iocom, NULL,
				       DMSG_LNK_SPAN | DMSGF_CREATE,
				       kdmsg_lnk_span_reply, NULL);
		iocom->auto_lnk_span.head = rmsg->any.head;
		rmsg->any.lnk_span = iocom->auto_lnk_span;
		kdmsg_msg_write(rmsg);
	}

	/*
	 * Process shim after the CONN is acknowledged and before the CONN
	 * transaction is deleted.  For deletions this gives device drivers
	 * the ability to interlock new operations on the circuit before
	 * it becomes illegal and panics.
	 */
	if (iocom->auto_callback)
		iocom->auto_callback(msg);

	if ((state->txcmd & DMSGF_DELETE) == 0 &&
	    (msg->any.head.cmd & DMSGF_DELETE)) {
		iocom->conn_state = NULL;
		kdmsg_msg_reply(msg, 0);
	}

	return (0);
}

static
int
kdmsg_lnk_span_reply(kdmsg_state_t *state, kdmsg_msg_t *msg)
{
	/*
	 * Be sure to process shim before terminating the SPAN
	 * transaction.  Gives device drivers the ability to
	 * interlock new operations on the circuit before it
	 * becomes illegal and panics.
	 */
	if (state->iocom->auto_callback)
		state->iocom->auto_callback(msg);

	if ((state->txcmd & DMSGF_DELETE) == 0 &&
	    (msg->any.head.cmd & DMSGF_DELETE)) {
		kdmsg_msg_reply(msg, 0);
	}
	return (0);
}

/*
 * Disconnect and clean up
 */
void
kdmsg_iocom_uninit(kdmsg_iocom_t *iocom)
{
	kdmsg_state_t *state;

	/*
	 * Ask the cluster controller to go away
	 */
	lockmgr(&iocom->msglk, LK_EXCLUSIVE);
	atomic_set_int(&iocom->msg_ctl, KDMSG_CLUSTERCTL_KILL);

	while (iocom->msgrd_td || iocom->msgwr_td) {
		wakeup(&iocom->msg_ctl);
		lksleep(iocom, &iocom->msglk, 0, "clstrkl", hz);
	}

	/*
	 * Cleanup caches
	 */
	if ((state = iocom->freerd_state) != NULL) {
		iocom->freerd_state = NULL;
		kdmsg_state_free(state);
	}

	if ((state = iocom->freewr_state) != NULL) {
		iocom->freewr_state = NULL;
		kdmsg_state_free(state);
	}

	/*
	 * Drop communications descriptor
	 */
	if (iocom->msg_fp) {
		fdrop(iocom->msg_fp);
		iocom->msg_fp = NULL;
	}
	lockmgr(&iocom->msglk, LK_RELEASE);
}

/*
 * Cluster controller thread.  Perform messaging functions.  We have one
 * thread for the reader and one for the writer.  The writer handles
 * shutdown requests (which should break the reader thread).
 */
static
void
kdmsg_iocom_thread_rd(void *arg)
{
	kdmsg_iocom_t *iocom = arg;
	dmsg_hdr_t hdr;
	kdmsg_msg_t *msg = NULL;
	size_t hbytes;
	size_t abytes;
	int error = 0;

	while ((iocom->msg_ctl & KDMSG_CLUSTERCTL_KILL) == 0) {
		/*
		 * Retrieve the message from the pipe or socket.
		 */
		error = fp_read(iocom->msg_fp, &hdr, sizeof(hdr),
				NULL, 1, UIO_SYSSPACE);
		if (error)
			break;
		if (hdr.magic != DMSG_HDR_MAGIC) {
			kprintf("kdmsg: bad magic: %04x\n", hdr.magic);
			error = EINVAL;
			break;
		}
		hbytes = (hdr.cmd & DMSGF_SIZE) * DMSG_ALIGN;
		if (hbytes < sizeof(hdr) || hbytes > DMSG_AUX_MAX) {
			kprintf("kdmsg: bad header size %zd\n", hbytes);
			error = EINVAL;
			break;
		}
		/* XXX messy: mask cmd to avoid allocating state */
		msg = kdmsg_msg_alloc(iocom, NULL,
				      hdr.cmd & DMSGF_BASECMDMASK,
				      NULL, NULL);
		msg->any.head = hdr;
		msg->hdr_size = hbytes;
		if (hbytes > sizeof(hdr)) {
			error = fp_read(iocom->msg_fp, &msg->any.head + 1,
					hbytes - sizeof(hdr),
					NULL, 1, UIO_SYSSPACE);
			if (error) {
				kprintf("kdmsg: short msg received\n");
				error = EINVAL;
				break;
			}
		}
		msg->aux_size = hdr.aux_bytes;
		if (msg->aux_size > DMSG_AUX_MAX) {
			kprintf("kdmsg: illegal msg payload size %zd\n",
				msg->aux_size);
			error = EINVAL;
			break;
		}
		if (msg->aux_size) {
			abytes = DMSG_DOALIGN(msg->aux_size);
			msg->aux_data = kmalloc(abytes, iocom->mmsg, M_WAITOK);
			msg->flags |= KDMSG_FLAG_AUXALLOC;
			error = fp_read(iocom->msg_fp, msg->aux_data,
					abytes, NULL, 1, UIO_SYSSPACE);
			if (error) {
				kprintf("kdmsg: short msg payload received\n");
				break;
			}
		}

		(void)kdmsg_circ_msgrx(msg);
		error = kdmsg_msg_receive_handling(msg);
		msg = NULL;
	}

	if (error)
		kprintf("kdmsg: read failed error %d\n", error);

	lockmgr(&iocom->msglk, LK_EXCLUSIVE);
	if (msg)
		kdmsg_msg_free(msg);

	/*
	 * Shutdown the socket before waiting for the transmit side.
	 *
	 * If we are dying due to e.g. a socket disconnect verses being
	 * killed explicity we have to set KILL in order to kick the tx
	 * side when it might not have any other work to do.  KILL might
	 * already be set if we are in an unmount or reconnect.
	 */
	fp_shutdown(iocom->msg_fp, SHUT_RDWR);

	atomic_set_int(&iocom->msg_ctl, KDMSG_CLUSTERCTL_KILL);
	wakeup(&iocom->msg_ctl);

	/*
	 * Wait for the transmit side to drain remaining messages
	 * before cleaning up the rx state.  The transmit side will
	 * set KILLTX and wait for the rx side to completely finish
	 * (set msgrd_td to NULL) before cleaning up any remaining
	 * tx states.
	 */
	lockmgr(&iocom->msglk, LK_RELEASE);
	atomic_set_int(&iocom->msg_ctl, KDMSG_CLUSTERCTL_KILLRX);
	wakeup(&iocom->msg_ctl);
	while ((iocom->msg_ctl & KDMSG_CLUSTERCTL_KILLTX) == 0) {
		wakeup(&iocom->msg_ctl);
		tsleep(iocom, 0, "clstrkw", hz);
	}

	iocom->msgrd_td = NULL;

	/*
	 * iocom can be ripped out from under us at this point but
	 * wakeup() is safe.
	 */
	wakeup(iocom);
	lwkt_exit();
}

static
void
kdmsg_iocom_thread_wr(void *arg)
{
	kdmsg_iocom_t *iocom = arg;
	kdmsg_msg_t *msg;
	kdmsg_state_t *state;
	ssize_t res;
	size_t abytes;
	int error = 0;
	int retries = 20;

	/*
	 * Transmit loop
	 */
	msg = NULL;
	lockmgr(&iocom->msglk, LK_EXCLUSIVE);

	while ((iocom->msg_ctl & KDMSG_CLUSTERCTL_KILL) == 0 && error == 0) {
		/*
		 * Sleep if no messages pending.  Interlock with flag while
		 * holding msglk.
		 */
		if (TAILQ_EMPTY(&iocom->msgq)) {
			atomic_set_int(&iocom->msg_ctl,
				       KDMSG_CLUSTERCTL_SLEEPING);
			lksleep(&iocom->msg_ctl, &iocom->msglk, 0, "msgwr", hz);
			atomic_clear_int(&iocom->msg_ctl,
					 KDMSG_CLUSTERCTL_SLEEPING);
		}

		while ((msg = TAILQ_FIRST(&iocom->msgq)) != NULL) {
			/*
			 * Remove msg from the transmit queue and do
			 * persist and half-closed state handling.
			 */
			TAILQ_REMOVE(&iocom->msgq, msg, qentry);
			lockmgr(&iocom->msglk, LK_RELEASE);

			error = kdmsg_state_msgtx(msg);
			if (error == EALREADY) {
				error = 0;
				kdmsg_msg_free(msg);
				lockmgr(&iocom->msglk, LK_EXCLUSIVE);
				continue;
			}
			if (error) {
				kdmsg_msg_free(msg);
				lockmgr(&iocom->msglk, LK_EXCLUSIVE);
				break;
			}

			/*
			 * Dump the message to the pipe or socket.
			 *
			 * We have to clean up the message as if the transmit
			 * succeeded even if it failed.
			 */
			error = fp_write(iocom->msg_fp, &msg->any,
					 msg->hdr_size, &res, UIO_SYSSPACE);
			if (error || res != msg->hdr_size) {
				if (error == 0)
					error = EINVAL;
				kdmsg_state_cleanuptx(msg);
				lockmgr(&iocom->msglk, LK_EXCLUSIVE);
				break;
			}
			if (msg->aux_size) {
				abytes = DMSG_DOALIGN(msg->aux_size);
				error = fp_write(iocom->msg_fp,
						 msg->aux_data, abytes,
						 &res, UIO_SYSSPACE);
				if (error || res != abytes) {
					if (error == 0)
						error = EINVAL;
					kdmsg_state_cleanuptx(msg);
					lockmgr(&iocom->msglk, LK_EXCLUSIVE);
					break;
				}
			}
			kdmsg_state_cleanuptx(msg);
			lockmgr(&iocom->msglk, LK_EXCLUSIVE);
		}
	}

	/*
	 * Cleanup messages pending transmission and release msgq lock.
	 */
	if (error)
		kprintf("kdmsg: write failed error %d\n", error);
	kprintf("thread_wr: Terminating iocom\n");

	/*
	 * Shutdown the socket.  This will cause the rx thread to get an
	 * EOF and ensure that both threads get to a termination state.
	 */
	fp_shutdown(iocom->msg_fp, SHUT_RDWR);

	/*
	 * Set KILLTX (which the rx side waits for), then wait for the RX
	 * side to completely finish before we clean out any remaining
	 * command states.
	 */
	lockmgr(&iocom->msglk, LK_RELEASE);
	atomic_set_int(&iocom->msg_ctl, KDMSG_CLUSTERCTL_KILLTX);
	wakeup(&iocom->msg_ctl);
	while (iocom->msgrd_td) {
		wakeup(&iocom->msg_ctl);
		tsleep(iocom, 0, "clstrkw", hz);
	}
	lockmgr(&iocom->msglk, LK_EXCLUSIVE);

	/*
	 * Simulate received MSGF_DELETE's for any remaining states.
	 * (For remote masters).
	 *
	 * Drain the message queue to handle any device initiated writes
	 * due to state callbacks.
	 */
cleanuprd:
	kdmsg_drain_msgq(iocom);
	RB_FOREACH(state, kdmsg_state_tree, &iocom->staterd_tree) {
		if ((state->rxcmd & DMSGF_DELETE) == 0) {
			lockmgr(&iocom->msglk, LK_RELEASE);
			kdmsg_state_abort(state);
			lockmgr(&iocom->msglk, LK_EXCLUSIVE);
			goto cleanuprd;
		}
	}

	/*
	 * Simulate received MSGF_DELETE's for any remaining states.
	 * (For local masters).
	 */
cleanupwr:
	kdmsg_drain_msgq(iocom);
	RB_FOREACH(state, kdmsg_state_tree, &iocom->statewr_tree) {
		if ((state->rxcmd & DMSGF_DELETE) == 0) {
			lockmgr(&iocom->msglk, LK_RELEASE);
			kdmsg_state_abort(state);
			lockmgr(&iocom->msglk, LK_EXCLUSIVE);
			goto cleanupwr;
		}
	}

	/*
	 * Retry until all work is done
	 */
	if (--retries == 0)
		panic("kdmsg: comm thread shutdown couldn't drain");
	if (TAILQ_FIRST(&iocom->msgq) ||
	    RB_ROOT(&iocom->staterd_tree) ||
	    RB_ROOT(&iocom->statewr_tree)) {
		goto cleanuprd;
	}
	iocom->flags |= KDMSG_IOCOMF_EXITNOACC;

	lockmgr(&iocom->msglk, LK_RELEASE);

	/*
	 * The state trees had better be empty now
	 */
	KKASSERT(RB_EMPTY(&iocom->staterd_tree));
	KKASSERT(RB_EMPTY(&iocom->statewr_tree));
	KKASSERT(iocom->conn_state == NULL);

	if (iocom->exit_func) {
		/*
		 * iocom is invalid after we call the exit function.
		 */
		iocom->msgwr_td = NULL;
		iocom->exit_func(iocom);
	} else {
		/*
		 * iocom can be ripped out from under us once msgwr_td is
		 * set to NULL.  The wakeup is safe.
		 */
		iocom->msgwr_td = NULL;
		wakeup(iocom);
	}
	lwkt_exit();
}

/*
 * This cleans out the pending transmit message queue, adjusting any
 * persistent states properly in the process.
 *
 * Caller must hold pmp->iocom.msglk
 */
void
kdmsg_drain_msgq(kdmsg_iocom_t *iocom)
{
	kdmsg_msg_t *msg;

	/*
	 * Clean out our pending transmit queue, executing the
	 * appropriate state adjustments.  If this tries to open
	 * any new outgoing transactions we have to loop up and
	 * clean them out.
	 */
	while ((msg = TAILQ_FIRST(&iocom->msgq)) != NULL) {
		TAILQ_REMOVE(&iocom->msgq, msg, qentry);
		lockmgr(&iocom->msglk, LK_RELEASE);
		if (kdmsg_state_msgtx(msg))
			kdmsg_msg_free(msg);
		else
			kdmsg_state_cleanuptx(msg);
		lockmgr(&iocom->msglk, LK_EXCLUSIVE);
	}
}

/*
 * Do all processing required to handle a freshly received message
 * after its low level header has been validated.
 */
static
int
kdmsg_msg_receive_handling(kdmsg_msg_t *msg)
{
	kdmsg_iocom_t *iocom = msg->iocom;
	int error;

	/*
	 * State machine tracking, state assignment for msg,
	 * returns error and discard status.  Errors are fatal
	 * to the connection except for EALREADY which forces
	 * a discard without execution.
	 */
	error = kdmsg_state_msgrx(msg);
	if (error) {
		/*
		 * Raw protocol or connection error
		 */
		kdmsg_msg_free(msg);
		if (error == EALREADY)
			error = 0;
	} else if (msg->state && msg->state->func) {
		/*
		 * Message related to state which already has a
		 * handling function installed for it.
		 */
		error = msg->state->func(msg->state, msg);
		kdmsg_state_cleanuprx(msg);
	} else if (iocom->flags & KDMSG_IOCOMF_AUTOANY) {
		error = kdmsg_autorxmsg(msg);
		kdmsg_state_cleanuprx(msg);
	} else {
		error = iocom->rcvmsg(msg);
		kdmsg_state_cleanuprx(msg);
	}
	return error;
}

/*
 * Process circuit tracking (NEEDS WORK)
 */
static
int
kdmsg_circ_msgrx(kdmsg_msg_t *msg)
{
	kdmsg_circuit_t dummy;
	kdmsg_circuit_t *circ;
	int error = 0;

	if (msg->any.head.circuit) {
		dummy.msgid = msg->any.head.circuit;
		lwkt_gettoken(&kdmsg_token);
		circ = RB_FIND(kdmsg_circuit_tree, &msg->iocom->circ_tree,
			       &dummy);
		if (circ) {
			msg->circ = circ;
			kdmsg_circ_hold(circ);
		}
		if (circ == NULL) {
			kprintf("KDMSG_CIRC_MSGRX CMD %08x: IOCOM %p "
				"Bad circuit %016jx\n",
				msg->any.head.cmd,
				msg->iocom,
				(intmax_t)msg->any.head.circuit);
			kprintf("KDMSG_CIRC_MSGRX: Avail circuits: ");
			RB_FOREACH(circ, kdmsg_circuit_tree,
				   &msg->iocom->circ_tree) {
				kprintf(" %016jx", (intmax_t)circ->msgid);
			}
			kprintf("\n");
			error = EINVAL;
		}
		lwkt_reltoken(&kdmsg_token);
	}
	return (error);
}

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
static
int
kdmsg_state_msgrx(kdmsg_msg_t *msg)
{
	kdmsg_iocom_t *iocom = msg->iocom;
	kdmsg_state_t *state;
	int error;

	/*
	 * Make sure a state structure is ready to go in case we need a new
	 * one.  This is the only routine which uses freerd_state so no
	 * races are possible.
	 */
	if ((state = iocom->freerd_state) == NULL) {
		state = kmalloc(sizeof(*state), iocom->mmsg, M_WAITOK | M_ZERO);
		state->flags = KDMSG_STATE_DYNAMIC;
		iocom->freerd_state = state;
	}

	/*
	 * Lock RB tree and locate existing persistent state, if any.
	 *
	 * If received msg is a command state is on staterd_tree.
	 * If received msg is a reply state is on statewr_tree.
	 */
	lockmgr(&iocom->msglk, LK_EXCLUSIVE);

	state->msgid = msg->any.head.msgid;
	state->circ = msg->circ;
	state->iocom = iocom;
	if (msg->any.head.cmd & DMSGF_REPLY)
		state = RB_FIND(kdmsg_state_tree, &iocom->statewr_tree, state);
	else
		state = RB_FIND(kdmsg_state_tree, &iocom->staterd_tree, state);
	msg->state = state;

	/*
	 * Short-cut one-off or mid-stream messages (state may be NULL).
	 */
	if ((msg->any.head.cmd & (DMSGF_CREATE | DMSGF_DELETE |
				  DMSGF_ABORT)) == 0) {
		lockmgr(&iocom->msglk, LK_RELEASE);
		return(0);
	}

	/*
	 * Switch on CREATE, DELETE, REPLY, and also handle ABORT from
	 * inside the case statements.
	 */
	switch(msg->any.head.cmd & (DMSGF_CREATE|DMSGF_DELETE|DMSGF_REPLY)) {
	case DMSGF_CREATE:
	case DMSGF_CREATE | DMSGF_DELETE:
		/*
		 * New persistant command received.
		 */
		if (state) {
			kprintf("kdmsg_state_msgrx: duplicate transaction\n");
			error = EINVAL;
			break;
		}
		state = iocom->freerd_state;
		iocom->freerd_state = NULL;
		msg->state = state;
		state->msg = msg;
		state->icmd = msg->any.head.cmd & DMSGF_BASECMDMASK;
		state->rxcmd = msg->any.head.cmd & ~DMSGF_DELETE;
		state->txcmd = DMSGF_REPLY;
		state->msgid = msg->any.head.msgid;
		if ((state->circ = msg->circ) != NULL)
			kdmsg_circ_hold(state->circ);
		RB_INSERT(kdmsg_state_tree, &iocom->staterd_tree, state);
		state->flags |= KDMSG_STATE_INSERTED;
		error = 0;
		break;
	case DMSGF_DELETE:
		/*
		 * Persistent state is expected but might not exist if an
		 * ABORT+DELETE races the close.
		 */
		if (state == NULL) {
			if (msg->any.head.cmd & DMSGF_ABORT) {
				error = EALREADY;
			} else {
				kprintf("kdmsg_state_msgrx: "
					"no state for DELETE\n");
				error = EINVAL;
			}
			break;
		}

		/*
		 * Handle another ABORT+DELETE case if the msgid has already
		 * been reused.
		 */
		if ((state->rxcmd & DMSGF_CREATE) == 0) {
			if (msg->any.head.cmd & DMSGF_ABORT) {
				error = EALREADY;
			} else {
				kprintf("kdmsg_state_msgrx: "
					"state reused for DELETE\n");
				error = EINVAL;
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
			if (state == NULL ||
			    (state->rxcmd & DMSGF_CREATE) == 0) {
				error = EALREADY;
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
		if (state == NULL) {
			kprintf("kdmsg_state_msgrx: no state match for "
				"REPLY cmd=%08x msgid=%016jx\n",
				msg->any.head.cmd,
				(intmax_t)msg->any.head.msgid);
			error = EINVAL;
			break;
		}
		state->rxcmd = msg->any.head.cmd & ~DMSGF_DELETE;
		error = 0;
		break;
	case DMSGF_REPLY | DMSGF_DELETE:
		/*
		 * Received REPLY+ABORT+DELETE in case where msgid has
		 * already been fully closed, ignore the message.
		 */
		if (state == NULL) {
			if (msg->any.head.cmd & DMSGF_ABORT) {
				error = EALREADY;
			} else {
				kprintf("kdmsg_state_msgrx: no state match "
					"for REPLY|DELETE\n");
				error = EINVAL;
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
				error = EALREADY;
			} else {
				kprintf("kdmsg_state_msgrx: state reused "
					"for REPLY|DELETE\n");
				error = EINVAL;
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
			if (state == NULL ||
			    (state->rxcmd & DMSGF_CREATE) == 0) {
				error = EALREADY;
				break;
			}
		}
		error = 0;
		break;
	}
	lockmgr(&iocom->msglk, LK_RELEASE);
	return (error);
}

/*
 * Called instead of iocom->rcvmsg() if any of the AUTO flags are set.
 * This routine must call iocom->rcvmsg() for anything not automatically
 * handled.
 */
static int
kdmsg_autorxmsg(kdmsg_msg_t *msg)
{
	kdmsg_iocom_t *iocom = msg->iocom;
	kdmsg_circuit_t *circ;
	int error = 0;
	uint32_t cmd;

	/*
	 * Process a combination of the transaction command and the message
	 * flags.  For the purposes of this routine, the message command is
	 * only relevant when it initiates a transaction (where it is
	 * recorded in icmd).
	 */
	cmd = (msg->state ? msg->state->icmd : msg->any.head.cmd) &
	      DMSGF_BASECMDMASK;
	cmd |= msg->any.head.cmd & (DMSGF_CREATE | DMSGF_DELETE | DMSGF_REPLY);

	switch(cmd) {
	case DMSG_LNK_CONN | DMSGF_CREATE:
	case DMSG_LNK_CONN | DMSGF_CREATE | DMSGF_DELETE:
		/*
		 * Received LNK_CONN transaction.  Transmit response and
		 * leave transaction open, which allows the other end to
		 * start to the SPAN protocol.
		 *
		 * Handle shim after acknowledging the CONN.
		 */
		if ((msg->any.head.cmd & DMSGF_DELETE) == 0) {
			if (iocom->flags & KDMSG_IOCOMF_AUTOCONN) {
				kdmsg_msg_result(msg, 0);
				if (iocom->auto_callback)
					iocom->auto_callback(msg);
			} else {
				error = iocom->rcvmsg(msg);
			}
			break;
		}
		/* fall through */
	case DMSG_LNK_CONN | DMSGF_DELETE:
		/*
		 * This message is usually simulated after a link is lost
		 * to clean up the transaction.
		 */
		if (iocom->flags & KDMSG_IOCOMF_AUTOCONN) {
			if (iocom->auto_callback)
				iocom->auto_callback(msg);
			kdmsg_msg_reply(msg, 0);
		} else {
			error = iocom->rcvmsg(msg);
		}
		break;
	case DMSG_LNK_SPAN | DMSGF_CREATE:
	case DMSG_LNK_SPAN | DMSGF_CREATE | DMSGF_DELETE:
		/*
		 * Received LNK_SPAN transaction.  We do not have to respond
		 * but we must leave the transaction open.
		 *
		 * If AUTOCIRC is set automatically initiate a virtual circuit
		 * to the received span.  This will attach a kdmsg_circuit
		 * to the SPAN state.  The circuit is lost when the span is
		 * lost.
		 *
		 * Handle shim after acknowledging the SPAN.
		 */
		if (iocom->flags & KDMSG_IOCOMF_AUTOSPAN) {
			if ((msg->any.head.cmd & DMSGF_DELETE) == 0) {
				if (iocom->flags & KDMSG_IOCOMF_AUTOFORGE)
					kdmsg_autocirc(msg);
				if (iocom->auto_callback)
					iocom->auto_callback(msg);
				break;
			}
			/* fall through */
		} else {
			error = iocom->rcvmsg(msg);
			break;
		}
		/* fall through */
	case DMSG_LNK_SPAN | DMSGF_DELETE:
		/*
		 * Process shims (auto_callback) before cleaning up the
		 * circuit structure and closing the transactions.  Device
		 * driver should ensure that the circuit is not used after
		 * the auto_callback() returns.
		 *
		 * Handle shim before closing the SPAN transaction.
		 */
		if (iocom->flags & KDMSG_IOCOMF_AUTOSPAN) {
			if (iocom->auto_callback)
				iocom->auto_callback(msg);
			if (iocom->flags & KDMSG_IOCOMF_AUTOFORGE)
				kdmsg_autocirc(msg);
			kdmsg_msg_reply(msg, 0);
		} else {
			error = iocom->rcvmsg(msg);
		}
		break;
	case DMSG_LNK_CIRC | DMSGF_CREATE:
	case DMSG_LNK_CIRC | DMSGF_CREATE | DMSGF_DELETE:
		/*
		 * Received LNK_CIRC transaction.  We must respond and should
		 * leave the transaction open, allowing the circuit.  The
		 * remote can start issuing commands to us over the circuit
		 * even before we respond.
		 */
		if (iocom->flags & KDMSG_IOCOMF_AUTOCIRC) {
			if ((msg->any.head.cmd & DMSGF_DELETE) == 0) {
				circ = kmalloc(sizeof(*circ), iocom->mmsg,
					       M_WAITOK | M_ZERO);
				lwkt_gettoken(&kdmsg_token);
				msg->state->any.circ = circ;
				circ->iocom = iocom;
				circ->rcirc_state = msg->state;
				kdmsg_circ_hold(circ);	/* for rcirc_state */
				circ->weight = 0;
				circ->msgid = circ->rcirc_state->msgid;
				/* XXX no span link for received circuits */
				kdmsg_circ_hold(circ);	/* for circ_state */

				if (RB_INSERT(kdmsg_circuit_tree,
					      &iocom->circ_tree, circ)) {
					panic("duplicate circuitid allocated");
				}
				lwkt_reltoken(&kdmsg_token);
				kdmsg_msg_result(msg, 0);

				/*
				 * Handle shim after adding the circuit and
				 * after acknowledging the CIRC.
				 */
				if (iocom->auto_callback)
					iocom->auto_callback(msg);
				break;
			}
			/* fall through */
		} else {
			error = iocom->rcvmsg(msg);
			break;
		}
		/* fall through */
	case DMSG_LNK_CIRC | DMSGF_DELETE:
		if (iocom->flags & KDMSG_IOCOMF_AUTOCIRC) {
			circ = msg->state->any.circ;
			if (circ == NULL)
				break;

			/*
			 * Handle shim before terminating the circuit.
			 */
#if 0
			kprintf("KDMSG VC: RECEIVE CIRC DELETE "
				"IOCOM %p MSGID %016jx\n",
				msg->iocom, circ->msgid);
#endif
			if (iocom->auto_callback)
				iocom->auto_callback(msg);

			KKASSERT(circ->rcirc_state == msg->state);
			lwkt_gettoken(&kdmsg_token);
			circ->rcirc_state = NULL;
			msg->state->any.circ = NULL;
			RB_REMOVE(kdmsg_circuit_tree, &iocom->circ_tree, circ);
			lwkt_reltoken(&kdmsg_token);
			kdmsg_circ_drop(circ);	/* for rcirc_state */
			kdmsg_msg_reply(msg, 0);
		} else {
			error = iocom->rcvmsg(msg);
		}
		break;
	default:
		/*
		 * Anything unhandled goes into rcvmsg.
		 *
		 * NOTE: Replies to link-level messages initiated by our side
		 *	 are handled by the state callback, they are NOT
		 *	 handled here.
		 */
		error = iocom->rcvmsg(msg);
		break;
	}
	return (error);
}

/*
 * Handle automatic forging of virtual circuits based on received SPANs.
 * (AUTOFORGE).  Note that other code handles tracking received circuit
 * transactions (AUTOCIRC).
 *
 * We can ignore non-transactions here.  Use trans->icmd to test the
 * transactional command (once past the CREATE the individual message
 * commands are not usually the icmd).
 *
 * XXX locks
 */
static
void
kdmsg_autocirc(kdmsg_msg_t *msg)
{
	kdmsg_iocom_t *iocom = msg->iocom;
	kdmsg_circuit_t *circ;
	kdmsg_msg_t *xmsg;	/* CIRC */

	if (msg->state == NULL)
		return;

	/*
	 * Gaining the SPAN, automatically forge a circuit to the target.
	 *
	 * NOTE!! The shim is not executed until we receive an acknowlegement
	 *	  to our forged LNK_CIRC (see kdmsg_autocirc_reply()).
	 */
	if (msg->state->icmd == DMSG_LNK_SPAN &&
	    (msg->any.head.cmd & DMSGF_CREATE)) {
		circ = kmalloc(sizeof(*circ), iocom->mmsg, M_WAITOK | M_ZERO);
		lwkt_gettoken(&kdmsg_token);
		msg->state->any.circ = circ;
		circ->iocom = iocom;
		circ->span_state = msg->state;
		kdmsg_circ_hold(circ);	/* for span_state */
		xmsg = kdmsg_msg_alloc(iocom, NULL,
				       DMSG_LNK_CIRC | DMSGF_CREATE,
				       kdmsg_autocirc_reply, circ);
		circ->circ_state = xmsg->state;
		circ->weight = msg->any.lnk_span.dist;
		circ->msgid = circ->circ_state->msgid;
		kdmsg_circ_hold(circ);	/* for circ_state */
#if 0
		kprintf("KDMSG VC: CREATE SPAN->CIRC IOCOM %p MSGID %016jx\n",
			msg->iocom, circ->msgid);
#endif

		if (RB_INSERT(kdmsg_circuit_tree, &iocom->circ_tree, circ))
			panic("duplicate circuitid allocated");
		lwkt_reltoken(&kdmsg_token);

		xmsg->any.lnk_circ.target = msg->any.head.msgid;
		kdmsg_msg_write(xmsg);
	}

	/*
	 * Losing the SPAN
	 *
	 * NOTE: When losing a SPAN, any circuits using the span should be
	 *	 deleted by the remote end first.  XXX might not be ordered
	 *	 on actual loss of connection.
	 */
	if (msg->state->icmd == DMSG_LNK_SPAN &&
	    (msg->any.head.cmd & DMSGF_DELETE) &&
	    msg->state->any.circ) {
		circ = msg->state->any.circ;
		lwkt_gettoken(&kdmsg_token);
		circ->span_state = NULL;
		msg->state->any.circ = NULL;
		RB_REMOVE(kdmsg_circuit_tree, &iocom->circ_tree, circ);
#if 0
		kprintf("KDMSG VC: DELETE SPAN->CIRC IOCOM %p MSGID %016jx\n",
			msg->iocom, (intmax_t)circ->msgid);
#endif
		kdmsg_circ_drop(circ);	/* for span_state */
		lwkt_reltoken(&kdmsg_token);
	}
}

static
int
kdmsg_autocirc_reply(kdmsg_state_t *state, kdmsg_msg_t *msg)
{
	kdmsg_iocom_t *iocom = state->iocom;
	kdmsg_circuit_t *circ = state->any.circ;

	/*
	 * Call shim after receiving an acknowlegement to our forged
	 * circuit and before processing a received termination.
	 */
	if (iocom->auto_callback)
		iocom->auto_callback(msg);

	/*
	 * If the remote is terminating the VC we terminate our side
	 */
	if ((state->txcmd & DMSGF_DELETE) == 0 &&
	    (msg->any.head.cmd & DMSGF_DELETE)) {
#if 0
		kprintf("KDMSG VC: DELETE CIRC FROM REMOTE\n");
#endif
		lwkt_gettoken(&kdmsg_token);
		circ->circ_state = NULL;
		state->any.circ = NULL;
		kdmsg_circ_drop(circ);		/* for circ_state */
		lwkt_reltoken(&kdmsg_token);
		kdmsg_msg_reply(msg, 0);
	}
	return (0);
}

/*
 * Post-receive-handling message and state cleanup.  This routine is called
 * after the state function handling/callback to properly dispose of the
 * message and update or dispose of the state.
 */
static
void
kdmsg_state_cleanuprx(kdmsg_msg_t *msg)
{
	kdmsg_iocom_t *iocom = msg->iocom;
	kdmsg_state_t *state;

	if ((state = msg->state) == NULL) {
		kdmsg_msg_free(msg);
	} else if (msg->any.head.cmd & DMSGF_DELETE) {
		lockmgr(&iocom->msglk, LK_EXCLUSIVE);
		KKASSERT((state->rxcmd & DMSGF_DELETE) == 0);
		state->rxcmd |= DMSGF_DELETE;
		if (state->txcmd & DMSGF_DELETE) {
			KKASSERT(state->flags & KDMSG_STATE_INSERTED);
			if (state->rxcmd & DMSGF_REPLY) {
				KKASSERT(msg->any.head.cmd &
					 DMSGF_REPLY);
				RB_REMOVE(kdmsg_state_tree,
					  &iocom->statewr_tree, state);
			} else {
				KKASSERT((msg->any.head.cmd &
					  DMSGF_REPLY) == 0);
				RB_REMOVE(kdmsg_state_tree,
					  &iocom->staterd_tree, state);
			}
			state->flags &= ~KDMSG_STATE_INSERTED;
			if (msg != state->msg)
				kdmsg_msg_free(msg);
			lockmgr(&iocom->msglk, LK_RELEASE);
			kdmsg_state_free(state);
		} else {
			if (msg != state->msg)
				kdmsg_msg_free(msg);
			lockmgr(&iocom->msglk, LK_RELEASE);
		}
	} else if (msg != state->msg) {
		kdmsg_msg_free(msg);
	}
}

/*
 * Simulate receiving a message which terminates an active transaction
 * state.  Our simulated received message must set DELETE and may also
 * have to set CREATE.  It must also ensure that all fields are set such
 * that the receive handling code can find the state (kdmsg_state_msgrx())
 * or an endless loop will ensue.
 *
 * This is used when the other end of the link or virtual circuit is dead
 * so the device driver gets a completed transaction for all pending states.
 */
static
void
kdmsg_state_abort(kdmsg_state_t *state)
{
	kdmsg_iocom_t *iocom = state->iocom;
	kdmsg_msg_t *msg;

	/*
	 * Prevent recursive aborts which could otherwise occur if the
	 * simulated message reception runs state->func which then turns
	 * around and tries to reply to a broken circuit when then calls
	 * the state abort code again.
	 */
	if (state->flags & KDMSG_STATE_ABORTING)
		return;
	state->flags |= KDMSG_STATE_ABORTING;

	/*
	 * Simulatem essage reception
	 */
	msg = kdmsg_msg_alloc(iocom, state->circ,
			      DMSG_LNK_ERROR,
			      NULL, NULL);
	if ((state->rxcmd & DMSGF_CREATE) == 0)
		msg->any.head.cmd |= DMSGF_CREATE;
	msg->any.head.cmd |= DMSGF_DELETE | (state->rxcmd & DMSGF_REPLY);
	msg->any.head.error = DMSG_ERR_LOSTLINK;
	msg->any.head.msgid = state->msgid;
	msg->state = state;
	kdmsg_msg_receive_handling(msg);
}

/*
 * Process state tracking for a message prior to transmission.
 *
 * Called with msglk held and the msg dequeued.  Returns non-zero if
 * the message is bad and should be deleted by the caller.
 *
 * One-off messages are usually with dummy state and msg->state may be NULL
 * in this situation.
 *
 * New transactions (when CREATE is set) will insert the state.
 *
 * May request that caller discard the message by setting *discardp to 1.
 * A NULL state may be returned in this case.
 */
static
int
kdmsg_state_msgtx(kdmsg_msg_t *msg)
{
	kdmsg_iocom_t *iocom = msg->iocom;
	kdmsg_state_t *state;
	int error;

	/*
	 * Make sure a state structure is ready to go in case we need a new
	 * one.  This is the only routine which uses freewr_state so no
	 * races are possible.
	 */
	if ((state = iocom->freewr_state) == NULL) {
		state = kmalloc(sizeof(*state), iocom->mmsg, M_WAITOK | M_ZERO);
		state->flags = KDMSG_STATE_DYNAMIC;
		state->iocom = iocom;
		iocom->freewr_state = state;
	}

	/*
	 * Lock RB tree.  If persistent state is present it will have already
	 * been assigned to msg.
	 */
	lockmgr(&iocom->msglk, LK_EXCLUSIVE);
	state = msg->state;

	/*
	 * Short-cut one-off or mid-stream messages (state may be NULL).
	 */
	if ((msg->any.head.cmd & (DMSGF_CREATE | DMSGF_DELETE |
				  DMSGF_ABORT)) == 0) {
		lockmgr(&iocom->msglk, LK_RELEASE);
		return(0);
	}


	/*
	 * Switch on CREATE, DELETE, REPLY, and also handle ABORT from
	 * inside the case statements.
	 */
	switch(msg->any.head.cmd & (DMSGF_CREATE | DMSGF_DELETE |
				    DMSGF_REPLY)) {
	case DMSGF_CREATE:
	case DMSGF_CREATE | DMSGF_DELETE:
		/*
		 * Insert the new persistent message state and mark
		 * half-closed if DELETE is set.  Since this is a new
		 * message it isn't possible to transition into the fully
		 * closed state here.
		 *
		 * XXX state must be assigned and inserted by
		 *     kdmsg_msg_write().  txcmd is assigned by us
		 *     on-transmit.
		 */
		KKASSERT(state != NULL);
		state->icmd = msg->any.head.cmd & DMSGF_BASECMDMASK;
		state->txcmd = msg->any.head.cmd & ~DMSGF_DELETE;
		state->rxcmd = DMSGF_REPLY;
		error = 0;
		break;
	case DMSGF_DELETE:
		/*
		 * Sent ABORT+DELETE in case where msgid has already
		 * been fully closed, ignore the message.
		 */
		if (state == NULL) {
			if (msg->any.head.cmd & DMSGF_ABORT) {
				error = EALREADY;
			} else {
				kprintf("kdmsg_state_msgtx: no state match "
					"for DELETE cmd=%08x msgid=%016jx\n",
					msg->any.head.cmd,
					(intmax_t)msg->any.head.msgid);
				error = EINVAL;
			}
			break;
		}

		/*
		 * Sent ABORT+DELETE in case where msgid has
		 * already been reused for an unrelated message,
		 * ignore the message.
		 */
		if ((state->txcmd & DMSGF_CREATE) == 0) {
			if (msg->any.head.cmd & DMSGF_ABORT) {
				error = EALREADY;
			} else {
				kprintf("kdmsg_state_msgtx: state reused "
					"for DELETE\n");
				error = EINVAL;
			}
			break;
		}
		error = 0;
		break;
	default:
		/*
		 * Check for mid-stream ABORT command sent
		 */
		if (msg->any.head.cmd & DMSGF_ABORT) {
			if (state == NULL ||
			    (state->txcmd & DMSGF_CREATE) == 0) {
				error = EALREADY;
				break;
			}
		}
		error = 0;
		break;
	case DMSGF_REPLY | DMSGF_CREATE:
	case DMSGF_REPLY | DMSGF_CREATE | DMSGF_DELETE:
		/*
		 * When transmitting a reply with CREATE set the original
		 * persistent state message should already exist.
		 */
		if (state == NULL) {
			kprintf("kdmsg_state_msgtx: no state match "
				"for REPLY | CREATE\n");
			error = EINVAL;
			break;
		}
		state->txcmd = msg->any.head.cmd & ~DMSGF_DELETE;
		error = 0;
		break;
	case DMSGF_REPLY | DMSGF_DELETE:
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
			if (msg->any.head.cmd & DMSGF_ABORT) {
				error = EALREADY;
			} else {
				kprintf("kdmsg_state_msgtx: no state match "
					"for REPLY | DELETE\n");
				error = EINVAL;
			}
			break;
		}

		/*
		 * Sent REPLY+ABORT+DELETE in case where msgid has already
		 * been reused for an unrelated message, ignore the message.
		 */
		if ((state->txcmd & DMSGF_CREATE) == 0) {
			if (msg->any.head.cmd & DMSGF_ABORT) {
				error = EALREADY;
			} else {
				kprintf("kdmsg_state_msgtx: state reused "
					"for REPLY | DELETE\n");
				error = EINVAL;
			}
			break;
		}
		error = 0;
		break;
	case DMSGF_REPLY:
		/*
		 * Check for mid-stream ABORT reply sent.
		 *
		 * One-off REPLY messages are allowed for e.g. status updates.
		 */
		if (msg->any.head.cmd & DMSGF_ABORT) {
			if (state == NULL ||
			    (state->txcmd & DMSGF_CREATE) == 0) {
				error = EALREADY;
				break;
			}
		}
		error = 0;
		break;
	}
	lockmgr(&iocom->msglk, LK_RELEASE);
	return (error);
}

static
void
kdmsg_state_cleanuptx(kdmsg_msg_t *msg)
{
	kdmsg_iocom_t *iocom = msg->iocom;
	kdmsg_state_t *state;

	if ((state = msg->state) == NULL) {
		kdmsg_msg_free(msg);
	} else if (msg->any.head.cmd & DMSGF_DELETE) {
		lockmgr(&iocom->msglk, LK_EXCLUSIVE);
		KKASSERT((state->txcmd & DMSGF_DELETE) == 0);
		state->txcmd |= DMSGF_DELETE;
		if (state->rxcmd & DMSGF_DELETE) {
			KKASSERT(state->flags & KDMSG_STATE_INSERTED);
			if (state->txcmd & DMSGF_REPLY) {
				KKASSERT(msg->any.head.cmd &
					 DMSGF_REPLY);
				RB_REMOVE(kdmsg_state_tree,
					  &iocom->staterd_tree, state);
			} else {
				KKASSERT((msg->any.head.cmd &
					  DMSGF_REPLY) == 0);
				RB_REMOVE(kdmsg_state_tree,
					  &iocom->statewr_tree, state);
			}
			state->flags &= ~KDMSG_STATE_INSERTED;
			if (msg != state->msg)
				kdmsg_msg_free(msg);
			lockmgr(&iocom->msglk, LK_RELEASE);
			kdmsg_state_free(state);
		} else {
			if (msg != state->msg)
				kdmsg_msg_free(msg);
			lockmgr(&iocom->msglk, LK_RELEASE);
		}
	} else if (msg != state->msg) {
		kdmsg_msg_free(msg);
	}
}

static
void
kdmsg_state_free(kdmsg_state_t *state)
{
	kdmsg_iocom_t *iocom = state->iocom;
	kdmsg_msg_t *msg;

	KKASSERT((state->flags & KDMSG_STATE_INSERTED) == 0);
	msg = state->msg;
	state->msg = NULL;
	kfree(state, iocom->mmsg);
	if (msg) {
		msg->state = NULL;
		kdmsg_msg_free(msg);
	}
}

kdmsg_msg_t *
kdmsg_msg_alloc(kdmsg_iocom_t *iocom, kdmsg_circuit_t *circ, uint32_t cmd,
		int (*func)(kdmsg_state_t *, kdmsg_msg_t *), void *data)
{
	kdmsg_msg_t *msg;
	kdmsg_state_t *state;
	size_t hbytes;

	KKASSERT(iocom != NULL);
	hbytes = (cmd & DMSGF_SIZE) * DMSG_ALIGN;
	msg = kmalloc(offsetof(struct kdmsg_msg, any) + hbytes,
		      iocom->mmsg, M_WAITOK | M_ZERO);
	msg->hdr_size = hbytes;
	msg->iocom = iocom;
	msg->any.head.magic = DMSG_HDR_MAGIC;
	msg->any.head.cmd = cmd;
	if (circ) {
		kdmsg_circ_hold(circ);
		msg->circ = circ;
		msg->any.head.circuit = circ->msgid;
	}

	if (cmd & DMSGF_CREATE) {
		/*
		 * New transaction, requires tracking state and a unique
		 * msgid to be allocated.
		 */
		KKASSERT(msg->state == NULL);
		state = kmalloc(sizeof(*state), iocom->mmsg, M_WAITOK | M_ZERO);
		state->flags = KDMSG_STATE_DYNAMIC;
		state->func = func;
		state->any.any = data;
		state->msg = msg;
		state->msgid = (uint64_t)(uintptr_t)state;
		state->circ = circ;
		state->iocom = iocom;
		msg->state = state;
		if (circ)
			kdmsg_circ_hold(circ);
		/*msg->any.head.msgid = state->msgid;XXX*/

		lockmgr(&iocom->msglk, LK_EXCLUSIVE);
		if (RB_INSERT(kdmsg_state_tree, &iocom->statewr_tree, state))
			panic("duplicate msgid allocated");
		state->flags |= KDMSG_STATE_INSERTED;
		msg->any.head.msgid = state->msgid;
		lockmgr(&iocom->msglk, LK_RELEASE);
	}
	return (msg);
}

kdmsg_msg_t *
kdmsg_msg_alloc_state(kdmsg_state_t *state, uint32_t cmd,
		      int (*func)(kdmsg_state_t *, kdmsg_msg_t *), void *data)
{
	kdmsg_iocom_t *iocom = state->iocom;
	kdmsg_msg_t *msg;
	size_t hbytes;

	KKASSERT(iocom != NULL);
	hbytes = (cmd & DMSGF_SIZE) * DMSG_ALIGN;
	msg = kmalloc(offsetof(struct kdmsg_msg, any) + hbytes,
		      iocom->mmsg, M_WAITOK | M_ZERO);
	msg->hdr_size = hbytes;
	msg->iocom = iocom;
	msg->any.head.magic = DMSG_HDR_MAGIC;
	msg->any.head.cmd = cmd;
	msg->state = state;
	if (state->circ) {
		kdmsg_circ_hold(state->circ);
		msg->circ = state->circ;
		msg->any.head.circuit = state->circ->msgid;
	}
	return(msg);
}

void
kdmsg_msg_free(kdmsg_msg_t *msg)
{
	kdmsg_iocom_t *iocom = msg->iocom;

	if ((msg->flags & KDMSG_FLAG_AUXALLOC) &&
	    msg->aux_data && msg->aux_size) {
		kfree(msg->aux_data, iocom->mmsg);
		msg->flags &= ~KDMSG_FLAG_AUXALLOC;
	}
	if (msg->circ) {
		kdmsg_circ_drop(msg->circ);
		msg->circ = NULL;
	}
	if (msg->state) {
		if (msg->state->msg == msg)
			msg->state->msg = NULL;
		msg->state = NULL;
	}
	msg->aux_data = NULL;
	msg->aux_size = 0;
	msg->iocom = NULL;
	kfree(msg, iocom->mmsg);
}

/*
 * Circuits are tracked in a red-black tree by their circuit id (msgid).
 */
int
kdmsg_circuit_cmp(kdmsg_circuit_t *circ1, kdmsg_circuit_t *circ2)
{
	if (circ1->msgid < circ2->msgid)
		return(-1);
	if (circ1->msgid > circ2->msgid)
		return(1);
	return (0);
}

/*
 * Indexed messages are stored in a red-black tree indexed by their
 * msgid.  Only persistent messages are indexed.
 */
int
kdmsg_state_cmp(kdmsg_state_t *state1, kdmsg_state_t *state2)
{
	if (state1->iocom < state2->iocom)
		return(-1);
	if (state1->iocom > state2->iocom)
		return(1);
	if (state1->circ < state2->circ)
		return(-1);
	if (state1->circ > state2->circ)
		return(1);
	if (state1->msgid < state2->msgid)
		return(-1);
	if (state1->msgid > state2->msgid)
		return(1);
	return(0);
}

/*
 * Write a message.  All requisit command flags have been set.
 *
 * If msg->state is non-NULL the message is written to the existing
 * transaction.  msgid will be set accordingly.
 *
 * If msg->state is NULL and CREATE is set new state is allocated and
 * (func, data) is installed.  A msgid is assigned.
 *
 * If msg->state is NULL and CREATE is not set the message is assumed
 * to be a one-way message.  The originator must assign the msgid
 * (or leave it 0, which is typical.
 *
 * This function merely queues the message to the management thread, it
 * does not write to the message socket/pipe.
 */
void
kdmsg_msg_write(kdmsg_msg_t *msg)
{
	kdmsg_iocom_t *iocom = msg->iocom;
	kdmsg_state_t *state;

	if (msg->state) {
		/*
		 * Continuance or termination of existing transaction.
		 * The transaction could have been initiated by either end.
		 *
		 * (Function callback and aux data for the receive side can
		 * be replaced or left alone).
		 */
		state = msg->state;
		msg->any.head.msgid = state->msgid;
		lockmgr(&iocom->msglk, LK_EXCLUSIVE);
	} else {
		/*
		 * One-off message (always uses msgid 0 to distinguish
		 * between a possibly lost in-transaction message due to
		 * competing aborts and a real one-off message?)
		 */
		state = NULL;
		msg->any.head.msgid = 0;
		lockmgr(&iocom->msglk, LK_EXCLUSIVE);
	}

	/*
	 * With AUTOCIRC and AUTOFORGE it is possible for the circuit to
	 * get ripped out in the rxthread while some other thread is
	 * holding a ref on it inbetween allocating and sending a dmsg.
	 */
	if (msg->circ && msg->circ->rcirc_state == NULL &&
	    (msg->circ->span_state == NULL || msg->circ->circ_state == NULL)) {
		kprintf("kdmsg_msg_write: Attempt to write message to "
		        "terminated circuit: msg %08x\n", msg->any.head.cmd);
		lockmgr(&iocom->msglk, LK_RELEASE);
		if (kdmsg_state_msgtx(msg)) {
			if (state == NULL || msg != state->msg)
				kdmsg_msg_free(msg);
		} else if ((msg->state->rxcmd & DMSGF_DELETE) == 0) {
			/* XXX SMP races simulating a response here */
			kdmsg_state_t *state = msg->state;
			kdmsg_state_cleanuptx(msg);
			kdmsg_state_abort(state);
		} else {
			kdmsg_state_cleanuptx(msg);
		}
		return;
	}

	/*
	 * This flag is not set until after the tx thread has drained
	 * the txmsgq and simulated responses.  After that point the
	 * txthread is dead and can no longer simulate responses.
	 *
	 * Device drivers should never try to send a message once this
	 * flag is set.  They should have detected (through the state
	 * closures) that the link is in trouble.
	 */
	if (iocom->flags & KDMSG_IOCOMF_EXITNOACC) {
		lockmgr(&iocom->msglk, LK_RELEASE);
		panic("kdmsg_msg_write: Attempt to write message to "
		      "terminated iocom\n");
	}

	/*
	 * Finish up the msg fields.  Note that msg->aux_size and the
	 * aux_bytes stored in the message header represent the unaligned
	 * (actual) bytes of data, but the buffer is sized to an aligned
	 * size and the CRC is generated over the aligned length.
	 */
	msg->any.head.salt = /* (random << 8) | */ (iocom->msg_seq & 255);
	++iocom->msg_seq;

	if (msg->aux_data && msg->aux_size) {
		uint32_t abytes = DMSG_DOALIGN(msg->aux_size);

		msg->any.head.aux_bytes = msg->aux_size;
		msg->any.head.aux_crc = iscsi_crc32(msg->aux_data, abytes);
	}
	msg->any.head.hdr_crc = 0;
	msg->any.head.hdr_crc = iscsi_crc32(msg->any.buf, msg->hdr_size);

	TAILQ_INSERT_TAIL(&iocom->msgq, msg, qentry);

	if (iocom->msg_ctl & KDMSG_CLUSTERCTL_SLEEPING) {
		atomic_clear_int(&iocom->msg_ctl,
				 KDMSG_CLUSTERCTL_SLEEPING);
		wakeup(&iocom->msg_ctl);
	}

	lockmgr(&iocom->msglk, LK_RELEASE);
}

/*
 * Reply to a message and terminate our side of the transaction.
 *
 * If msg->state is non-NULL we are replying to a one-way message.
 */
void
kdmsg_msg_reply(kdmsg_msg_t *msg, uint32_t error)
{
	kdmsg_state_t *state = msg->state;
	kdmsg_msg_t *nmsg;
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
	if (state) {
		if (state->txcmd & DMSGF_DELETE)
			return;
		if ((state->txcmd & DMSGF_CREATE) == 0)
			cmd |= DMSGF_CREATE;
		if (state->txcmd & DMSGF_REPLY)
			cmd |= DMSGF_REPLY;
		cmd |= DMSGF_DELETE;
	} else {
		if ((msg->any.head.cmd & DMSGF_REPLY) == 0)
			cmd |= DMSGF_REPLY;
	}

	/* XXX messy mask cmd to avoid allocating state */
	nmsg = kdmsg_msg_alloc_state(state, cmd, NULL, NULL);
	nmsg->any.head.error = error;
	kdmsg_msg_write(nmsg);
}

/*
 * Reply to a message and continue our side of the transaction.
 *
 * If msg->state is non-NULL we are replying to a one-way message and this
 * function degenerates into the same as kdmsg_msg_reply().
 */
void
kdmsg_msg_result(kdmsg_msg_t *msg, uint32_t error)
{
	kdmsg_state_t *state = msg->state;
	kdmsg_msg_t *nmsg;
	uint32_t cmd;

	/*
	 * Return a simple result code, do NOT terminate the transaction.
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
	if (state) {
		if (state->txcmd & DMSGF_DELETE)
			return;
		if ((state->txcmd & DMSGF_CREATE) == 0)
			cmd |= DMSGF_CREATE;
		if (state->txcmd & DMSGF_REPLY)
			cmd |= DMSGF_REPLY;
		/* continuing transaction, do not set MSGF_DELETE */
	} else {
		if ((msg->any.head.cmd & DMSGF_REPLY) == 0)
			cmd |= DMSGF_REPLY;
	}

	/* XXX messy mask cmd to avoid allocating state */
	nmsg = kdmsg_msg_alloc_state(state, cmd, NULL, NULL);
	nmsg->any.head.error = error;
	kdmsg_msg_write(nmsg);
}

/*
 * Reply to a message and terminate our side of the transaction.
 *
 * If msg->state is non-NULL we are replying to a one-way message.
 */
void
kdmsg_state_reply(kdmsg_state_t *state, uint32_t error)
{
	kdmsg_msg_t *nmsg;
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
	KKASSERT(state);
	if (state->txcmd & DMSGF_DELETE)
		return;
	if ((state->txcmd & DMSGF_CREATE) == 0)
		cmd |= DMSGF_CREATE;
	if (state->txcmd & DMSGF_REPLY)
		cmd |= DMSGF_REPLY;
	cmd |= DMSGF_DELETE;

	/* XXX messy mask cmd to avoid allocating state */
	nmsg = kdmsg_msg_alloc_state(state, cmd, NULL, NULL);
	nmsg->any.head.error = error;
	kdmsg_msg_write(nmsg);
}

/*
 * Reply to a message and continue our side of the transaction.
 *
 * If msg->state is non-NULL we are replying to a one-way message and this
 * function degenerates into the same as kdmsg_msg_reply().
 */
void
kdmsg_state_result(kdmsg_state_t *state, uint32_t error)
{
	kdmsg_msg_t *nmsg;
	uint32_t cmd;

	/*
	 * Return a simple result code, do NOT terminate the transaction.
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
	KKASSERT(state);
	if (state->txcmd & DMSGF_DELETE)
		return;
	if ((state->txcmd & DMSGF_CREATE) == 0)
		cmd |= DMSGF_CREATE;
	if (state->txcmd & DMSGF_REPLY)
		cmd |= DMSGF_REPLY;
	/* continuing transaction, do not set MSGF_DELETE */

	/* XXX messy mask cmd to avoid allocating state */
	nmsg = kdmsg_msg_alloc_state(state, cmd, NULL, NULL);
	nmsg->any.head.error = error;
	kdmsg_msg_write(nmsg);
}
