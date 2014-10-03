/*
 * Copyright (c) 2004-2007 The DragonFly Project.  All rights reserved.
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
 *
 * $DragonFly: src/sys/sys/syslink_msg2.h,v 1.4 2007/08/13 17:47:20 dillon Exp $
 */
/*
 * Helper inlines and macros for syslink message generation
 */

#ifndef _SYS_SYSLINK_MSG2_H_
#define _SYS_SYSLINK_MSG2_H_

#ifndef _SYS_SYSLINK_H_
#include <sys/syslink.h>
#endif
#ifndef _SYS_SYSLINK_MSG_H_
#include <sys/syslink_msg.h>
#endif

static __inline
struct syslink_elm *
sl_msg_init_cmd(struct syslink_msg *msg, sl_proto_t proto, sl_cmd_t cmd)
{
	msg->sm_proto = proto;
	msg->sm_bytes = 0;
	/* rlabel will be filled in by the transport layer */
	/* msgid will be filled in by the transport layer */
	msg->sm_head.se_cmd = cmd;
	msg->sm_head.se_bytes = sizeof(struct syslink_elm);
	msg->sm_head.se_aux = 0;
	return(&msg->sm_head);
}

static __inline
struct syslink_elm *
sl_msg_init_reply(struct syslink_msg *msg, struct syslink_msg *rep,
		  int32_t error)
{
	rep->sm_proto = msg->sm_proto | SM_PROTO_REPLY;
	rep->sm_bytes = 0;
	rep->sm_rlabel = msg->sm_rlabel;
	rep->sm_msgid = msg->sm_msgid;
	rep->sm_head.se_cmd = msg->sm_head.se_cmd | SE_CMDF_REPLY;
	rep->sm_head.se_bytes = sizeof(struct syslink_elm);
	rep->sm_head.se_aux = error;
	return(&rep->sm_head);
}

static __inline
void
sl_msg_fini(struct syslink_msg *msg)
{
	msg->sm_bytes = __offsetof(struct syslink_msg, sm_head) +
				 SL_MSG_ALIGN(msg->sm_head.se_bytes);
}

static __inline
struct syslink_elm *
sl_elm_make(struct syslink_elm *par, sl_cmd_t cmd, int bytes)
{
	struct syslink_elm *elm = (void *)((char *)par + par->se_bytes);

	KKASSERT((cmd & SE_CMDF_STRUCTURED) == 0);
	elm->se_cmd = cmd;
	elm->se_bytes = sizeof(*elm) + bytes;
	elm->se_aux = 0;

	par->se_bytes += SL_MSG_ALIGN(elm->se_bytes);
	return(elm);
}

static __inline
struct syslink_elm *
sl_elm_make_aux(struct syslink_elm *par, sl_cmd_t cmd, int auxdata)
{
	struct syslink_elm *elm = (void *)((char *)par + par->se_bytes);

	KKASSERT((cmd & SE_CMDF_STRUCTURED) == 0);
	elm->se_cmd = cmd;
	elm->se_bytes = sizeof(*elm);
	elm->se_aux = auxdata;

	par->se_bytes += SL_MSG_ALIGN(elm->se_bytes);
	return(elm);
}

/*
 * Push a new element given the current element, creating a recursion
 * under the current element.
 */
static __inline
struct syslink_elm *
sl_elm_push(struct syslink_elm *par, sl_cmd_t cmd)
{
	struct syslink_elm *elm = (void *)((char *)par + par->se_bytes);

	KKASSERT(cmd & SE_CMDF_STRUCTURED);
	elm->se_cmd = cmd;
	elm->se_bytes = sizeof(struct syslink_elm);
	elm->se_aux = 0;

	return(elm);
}

/*
 * Pop a previously pushed element.  Additional array elements may be
 * added to the parent by continuing to push/pop using the parent.
 */
static __inline
void
sl_elm_pop(struct syslink_elm *par, struct syslink_elm *elm)
{
	par->se_bytes = (char *)elm - (char *)par + elm->se_bytes;
}

#define SL_FOREACH_ELEMENT(par, elm)					\
	for (elm = par + 1; (char *)elm < (char *)par + par->se_bytes;	\
	     elm = (void *)((char *)elm + SL_MSG_ALIGN(elm->se_bytes)))

#endif
