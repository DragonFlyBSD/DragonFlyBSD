/*
 * Copyright (c) 2012-2014 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
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
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/mountctl.h>
#include <sys/dirent.h>
#include <sys/uio.h>

#include "hammer2.h"

int
hammer2_msg_adhoc_input(kdmsg_msg_t *msg)
{
	kprintf("ADHOC INPUT MSG %08x\n", msg->any.head.cmd);
	return(0);
}

int
hammer2_msg_dbg_rcvmsg(kdmsg_msg_t *msg)
{
	switch(msg->any.head.cmd & DMSGF_CMDSWMASK) {
	case DMSG_DBG_SHELL:
		/*
		 * Execute shell command (not supported atm)
		 *
		 * This is a one-way packet but if not (e.g. if part of
		 * a streaming transaction), we will have already closed
		 * our end.
		 */
		kdmsg_msg_reply(msg, DMSG_ERR_NOSUPP);
		break;
	case DMSG_DBG_SHELL | DMSGF_REPLY:
		/*
		 * Receive one or more replies to a shell command that we
		 * sent.
		 *
		 * This is a one-way packet but if not (e.g. if part of
		 * a streaming transaction), we will have already closed
		 * our end.
		 */
		if (msg->aux_data) {
			msg->aux_data[msg->aux_size - 1] = 0;
			kprintf("DEBUGMSG: %s\n", msg->aux_data);
		}
		break;
	default:
		/*
		 * We don't understand what is going on, issue a reply.
		 * This will take care of all left-over cases whether it
		 * is a transaction or one-way.
		 */
		kdmsg_msg_reply(msg, DMSG_ERR_NOSUPP);
		break;
	}
	return(0);
}
