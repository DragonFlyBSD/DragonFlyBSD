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

#include "hammer2.h"

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
int
hammer2_state_msgrx(hammer2_msg_t *msg)
{
	hammer2_pfsmount_t *pmp;
	hammer2_state_t *state;
	int error;

	pmp = msg->router->pmp;

	/*
	 * XXX resolve msg->any.head.source and msg->any.head.target
	 *     into LNK_SPAN references.
	 *
	 * XXX replace msg->router
	 */

	/*
	 * Make sure a state structure is ready to go in case we need a new
	 * one.  This is the only routine which uses freerd_state so no
	 * races are possible.
	 */
	if ((state = pmp->freerd_state) == NULL) {
		state = kmalloc(sizeof(*state), pmp->mmsg, M_WAITOK | M_ZERO);
		state->pmp = pmp;
		state->flags = HAMMER2_STATE_DYNAMIC;
		pmp->freerd_state = state;
	}

	/*
	 * Lock RB tree and locate existing persistent state, if any.
	 *
	 * If received msg is a command state is on staterd_tree.
	 * If received msg is a reply state is on statewr_tree.
	 */
	lockmgr(&pmp->msglk, LK_EXCLUSIVE);

	state->msgid = msg->any.head.msgid;
	state->router = &pmp->router;
	kprintf("received msg %08x msgid %jx source=%jx target=%jx\n",
		msg->any.head.cmd,
		(intmax_t)msg->any.head.msgid,
		(intmax_t)msg->any.head.source,
		(intmax_t)msg->any.head.target);
	if (msg->any.head.cmd & DMSGF_REPLY)
		state = RB_FIND(hammer2_state_tree, &pmp->statewr_tree, state);
	else
		state = RB_FIND(hammer2_state_tree, &pmp->staterd_tree, state);
	msg->state = state;

	/*
	 * Short-cut one-off or mid-stream messages (state may be NULL).
	 */
	if ((msg->any.head.cmd & (DMSGF_CREATE | DMSGF_DELETE |
				  DMSGF_ABORT)) == 0) {
		lockmgr(&pmp->msglk, LK_RELEASE);
		return(0);
	}

	/*
	 * Switch on CREATE, DELETE, REPLY, and also handle ABORT from
	 * inside the case statements.
	 */
	switch(msg->any.head.cmd & (DMSGF_CREATE | DMSGF_DELETE | DMSGF_REPLY)) {
	case DMSGF_CREATE:
	case DMSGF_CREATE | DMSGF_DELETE:
		/*
		 * New persistant command received.
		 */
		if (state) {
			kprintf("hammer2_state_msgrx: duplicate transaction\n");
			error = EINVAL;
			break;
		}
		state = pmp->freerd_state;
		pmp->freerd_state = NULL;
		msg->state = state;
		state->router = msg->router;
		state->msg = msg;
		state->rxcmd = msg->any.head.cmd & ~DMSGF_DELETE;
		state->txcmd = DMSGF_REPLY;
		RB_INSERT(hammer2_state_tree, &pmp->staterd_tree, state);
		state->flags |= HAMMER2_STATE_INSERTED;
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
				kprintf("hammer2_state_msgrx: no state "
					"for DELETE\n");
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
				kprintf("hammer2_state_msgrx: state reused "
					"for DELETE\n");
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
			kprintf("hammer2_state_msgrx: no state match for "
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
				kprintf("hammer2_state_msgrx: no state match "
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
				kprintf("hammer2_state_msgrx: state reused "
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
	lockmgr(&pmp->msglk, LK_RELEASE);
	return (error);
}

void
hammer2_state_cleanuprx(hammer2_msg_t *msg)
{
	hammer2_pfsmount_t *pmp = msg->router->pmp;
	hammer2_state_t *state;

	if ((state = msg->state) == NULL) {
		hammer2_msg_free(msg);
	} else if (msg->any.head.cmd & DMSGF_DELETE) {
		lockmgr(&pmp->msglk, LK_EXCLUSIVE);
		state->rxcmd |= DMSGF_DELETE;
		if (state->txcmd & DMSGF_DELETE) {
			if (state->msg == msg)
				state->msg = NULL;
			KKASSERT(state->flags & HAMMER2_STATE_INSERTED);
			if (state->rxcmd & DMSGF_REPLY) {
				KKASSERT(msg->any.head.cmd &
					 DMSGF_REPLY);
				RB_REMOVE(hammer2_state_tree,
					  &pmp->statewr_tree, state);
			} else {
				KKASSERT((msg->any.head.cmd &
					  DMSGF_REPLY) == 0);
				RB_REMOVE(hammer2_state_tree,
					  &pmp->staterd_tree, state);
			}
			state->flags &= ~HAMMER2_STATE_INSERTED;
			lockmgr(&pmp->msglk, LK_RELEASE);
			hammer2_state_free(state);
		} else {
			lockmgr(&pmp->msglk, LK_RELEASE);
		}
		hammer2_msg_free(msg);
	} else if (state->msg != msg) {
		hammer2_msg_free(msg);
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
int
hammer2_state_msgtx(hammer2_msg_t *msg)
{
	hammer2_pfsmount_t *pmp = msg->router->pmp;
	hammer2_state_t *state;
	int error;

	/*
	 * Make sure a state structure is ready to go in case we need a new
	 * one.  This is the only routine which uses freewr_state so no
	 * races are possible.
	 */
	if ((state = pmp->freewr_state) == NULL) {
		state = kmalloc(sizeof(*state), pmp->mmsg, M_WAITOK | M_ZERO);
		state->pmp = pmp;
		state->flags = HAMMER2_STATE_DYNAMIC;
		pmp->freewr_state = state;
	}

	/*
	 * Lock RB tree.  If persistent state is present it will have already
	 * been assigned to msg.
	 */
	lockmgr(&pmp->msglk, LK_EXCLUSIVE);
	state = msg->state;

	/*
	 * Short-cut one-off or mid-stream messages (state may be NULL).
	 */
	if ((msg->any.head.cmd & (DMSGF_CREATE | DMSGF_DELETE |
				  DMSGF_ABORT)) == 0) {
		lockmgr(&pmp->msglk, LK_RELEASE);
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
		 *     hammer2_msg_write().  txcmd is assigned by us
		 *     on-transmit.
		 */
		KKASSERT(state != NULL);
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
				kprintf("hammer2_state_msgtx: no state match "
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
				kprintf("hammer2_state_msgtx: state reused "
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
			kprintf("hammer2_state_msgtx: no state match "
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
				kprintf("hammer2_state_msgtx: no state match "
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
				kprintf("hammer2_state_msgtx: state reused "
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
	lockmgr(&pmp->msglk, LK_RELEASE);
	return (error);
}

void
hammer2_state_cleanuptx(hammer2_msg_t *msg)
{
	hammer2_pfsmount_t *pmp = msg->router->pmp;
	hammer2_state_t *state;

	if ((state = msg->state) == NULL) {
		hammer2_msg_free(msg);
	} else if (msg->any.head.cmd & DMSGF_DELETE) {
		lockmgr(&pmp->msglk, LK_EXCLUSIVE);
		state->txcmd |= DMSGF_DELETE;
		if (state->rxcmd & DMSGF_DELETE) {
			if (state->msg == msg)
				state->msg = NULL;
			KKASSERT(state->flags & HAMMER2_STATE_INSERTED);
			if (state->txcmd & DMSGF_REPLY) {
				KKASSERT(msg->any.head.cmd &
					 DMSGF_REPLY);
				RB_REMOVE(hammer2_state_tree,
					  &pmp->staterd_tree, state);
			} else {
				KKASSERT((msg->any.head.cmd &
					  DMSGF_REPLY) == 0);
				RB_REMOVE(hammer2_state_tree,
					  &pmp->statewr_tree, state);
			}
			state->flags &= ~HAMMER2_STATE_INSERTED;
			lockmgr(&pmp->msglk, LK_RELEASE);
			hammer2_state_free(state);
		} else {
			lockmgr(&pmp->msglk, LK_RELEASE);
		}
		hammer2_msg_free(msg);
	} else if (state->msg != msg) {
		hammer2_msg_free(msg);
	}
}

void
hammer2_state_free(hammer2_state_t *state)
{
	hammer2_pfsmount_t *pmp = state->pmp;
	hammer2_msg_t *msg;

	KKASSERT((state->flags & HAMMER2_STATE_INSERTED) == 0);
	msg = state->msg;
	state->msg = NULL;
	kfree(state, pmp->mmsg);
	if (msg)
		hammer2_msg_free(msg);
}

hammer2_msg_t *
hammer2_msg_alloc(hammer2_router_t *router, uint32_t cmd,
		  int (*func)(hammer2_state_t *, hammer2_msg_t *), void *data)
{
	hammer2_pfsmount_t *pmp = router->pmp;
	hammer2_msg_t *msg;
	hammer2_state_t *state;
	size_t hbytes;

	hbytes = (cmd & DMSGF_SIZE) * DMSG_ALIGN;
	msg = kmalloc(offsetof(struct hammer2_msg, any) + hbytes,
		      pmp->mmsg, M_WAITOK | M_ZERO);
	msg->hdr_size = hbytes;
	msg->router = router;
	KKASSERT(router != NULL);
	msg->any.head.magic = DMSG_HDR_MAGIC;
	msg->any.head.source = 0;
	msg->any.head.target = router->target;
	msg->any.head.cmd = cmd;

	if (cmd & DMSGF_CREATE) {
		/*
		 * New transaction, requires tracking state and a unique
		 * msgid to be allocated.
		 */
		KKASSERT(msg->state == NULL);
		state = kmalloc(sizeof(*state), pmp->mmsg, M_WAITOK | M_ZERO);
		state->pmp = pmp;
		state->flags = HAMMER2_STATE_DYNAMIC;
		state->func = func;
		state->any.any = data;
		state->msg = msg;
		state->msgid = (uint64_t)(uintptr_t)state;
		state->router = msg->router;
		msg->state = state;
		msg->any.head.source = 0;
		msg->any.head.target = state->router->target;
		msg->any.head.msgid = state->msgid;

		lockmgr(&pmp->msglk, LK_EXCLUSIVE);
		if (RB_INSERT(hammer2_state_tree, &pmp->statewr_tree, state))
			panic("duplicate msgid allocated");
		state->flags |= HAMMER2_STATE_INSERTED;
		msg->any.head.msgid = state->msgid;
		lockmgr(&pmp->msglk, LK_RELEASE);
	}

	return (msg);
}

void
hammer2_msg_free(hammer2_msg_t *msg)
{
	hammer2_pfsmount_t *pmp = msg->router->pmp;

	if (msg->aux_data && msg->aux_size) {
		kfree(msg->aux_data, pmp->mmsg);
		msg->aux_data = NULL;
		msg->aux_size = 0;
		msg->router = NULL;
	}
	kfree(msg, pmp->mmsg);
}

/*
 * Indexed messages are stored in a red-black tree indexed by their
 * msgid.  Only persistent messages are indexed.
 */
int
hammer2_state_cmp(hammer2_state_t *state1, hammer2_state_t *state2)
{
	if (state1->router < state2->router)
		return(-1);
	if (state1->router > state2->router)
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
hammer2_msg_write(hammer2_msg_t *msg)
{
	hammer2_pfsmount_t *pmp = msg->router->pmp;
	hammer2_state_t *state;

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
		msg->any.head.source = 0;
		msg->any.head.target = state->router->target;
		lockmgr(&pmp->msglk, LK_EXCLUSIVE);
	} else {
		/*
		 * One-off message (always uses msgid 0 to distinguish
		 * between a possibly lost in-transaction message due to
		 * competing aborts and a real one-off message?)
		 */
		msg->any.head.msgid = 0;
		msg->any.head.source = 0;
		msg->any.head.target = msg->router->target;
		lockmgr(&pmp->msglk, LK_EXCLUSIVE);
	}

	/*
	 * Finish up the msg fields
	 */
	msg->any.head.salt = /* (random << 8) | */ (pmp->msg_seq & 255);
	++pmp->msg_seq;

	msg->any.head.hdr_crc = 0;
	msg->any.head.hdr_crc = hammer2_icrc32(msg->any.buf, msg->hdr_size);

	TAILQ_INSERT_TAIL(&pmp->msgq, msg, qentry);
	hammer2_clusterctl_wakeup(pmp);
	lockmgr(&pmp->msglk, LK_RELEASE);
}

/*
 * Reply to a message and terminate our side of the transaction.
 *
 * If msg->state is non-NULL we are replying to a one-way message.
 */
void
hammer2_msg_reply(hammer2_msg_t *msg, uint32_t error)
{
	hammer2_state_t *state = msg->state;
	hammer2_msg_t *nmsg;
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
	kprintf("MSG_REPLY state=%p msg %08x\n", state, cmd);

	/* XXX messy mask cmd to avoid allocating state */
	nmsg = hammer2_msg_alloc(msg->router, cmd & DMSGF_BASECMDMASK,
				 NULL, NULL);
	nmsg->any.head.cmd = cmd;
	nmsg->any.head.error = error;
	nmsg->state = state;
	hammer2_msg_write(nmsg);
}

/*
 * Reply to a message and continue our side of the transaction.
 *
 * If msg->state is non-NULL we are replying to a one-way message and this
 * function degenerates into the same as hammer2_msg_reply().
 */
void
hammer2_msg_result(hammer2_msg_t *msg, uint32_t error)
{
	hammer2_state_t *state = msg->state;
	hammer2_msg_t *nmsg;
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
	nmsg = hammer2_msg_alloc(msg->router, cmd & DMSGF_BASECMDMASK,
				 NULL, NULL);
	nmsg->any.head.cmd = cmd;
	nmsg->any.head.error = error;
	nmsg->state = state;
	hammer2_msg_write(nmsg);
}
