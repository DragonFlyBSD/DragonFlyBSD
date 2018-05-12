 /*
 * Copyright (c) 2014 - 2018 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Bill Yuan <bycn82@dragonflybsd.org>
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
#ifndef _IP_FW3_BASIC_H
#define _IP_FW3_BASIC_H

#define MODULE_BASIC_ID		0
#define MODULE_BASIC_NAME 	"basic"

enum ipfw3_basic_opcodes {
	O_BASIC_ACCEPT,		/* accept */
	O_BASIC_DENY,		/* deny */
	O_BASIC_COUNT,		/* count */
	O_BASIC_SKIPTO,		/* skipto action->arg1	*/
	O_BASIC_FORWARD,	/* arg3 count of dest, arg1 type of fwd */

	O_BASIC_IN,		/* in */
	O_BASIC_OUT,		/* out */
	O_BASIC_VIA,		/* via */
	O_BASIC_XMIT,		/* xmit */
	O_BASIC_RECV,		/* recv */

	O_BASIC_PROTO,		/*  arg1=protocol	*/
	O_BASIC_IP_SRC,
	O_BASIC_IP_SRC_N_PORT,	/* src ip: src port */
	O_BASIC_IP_SRC_MASK,	/*  ip = IP/mask*/
	O_BASIC_IP_SRC_ME,	/*  me  */
	O_BASIC_IP_SRC_LOOKUP,	/*  from lookup table */

	O_BASIC_IP_DST,
	O_BASIC_IP_DST_N_PORT,	/* dst ip: dst port */
	O_BASIC_IP_DST_MASK,	/*  ip = IP/mask */
	O_BASIC_IP_DST_ME,	/*  me	*/
	O_BASIC_IP_DST_LOOKUP,	/*  to lookup table */

	O_BASIC_IP_SRCPORT,	/*  src-port */
	O_BASIC_IP_DSTPORT,	/*  dst-port */
	O_BASIC_PROB,		/*  probability 0~1*/
	O_BASIC_KEEP_STATE,	/*  */
	O_BASIC_CHECK_STATE,	/*  */
	O_BASIC_TAG,		/*  action, add tag info into mbuf */
	O_BASIC_UNTAG,		/*  action, remote tag from mbuf */
	O_BASIC_TAGGED,		/*  filter, check the tag info */

	O_BASIC_COMMENT,	/*  comment,behind action, no check */
};


#define IS_EXPIRED(state)  (state->lifetime > 0 && 			\
		(state->timestamp + state->lifetime) < time_second) ||	\
		((state->expiry != 0) && (state->expiry < time_second))


#ifdef _KERNEL



#include <net/ipfw3_basic/ip_fw3_state.h>


/* prototype of the checker functions */
void	check_count(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void	check_skipto(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void	check_forward(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void	check_in(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void	check_out(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void	check_via(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void	check_proto(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void	check_prob(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void	check_from(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void	check_from_lookup(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void	check_from_me(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void	check_from_mask(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void	check_to(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void	check_to_lookup(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void	check_to_me(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void	check_to_mask(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void	check_tag(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void	check_untag(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void	check_tagged(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void	check_src_port(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void	check_dst_port(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void	check_src_n_port(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void	check_dst_n_port(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);

/* prototype of the utility functions */
int 	match_state(ipfw_insn *cmd, struct ipfw_flow_id *fid,
		struct ipfw3_state *state);
int 	count_match_state(ipfw_insn *cmd, struct ipfw_flow_id *fid,
		struct ipfw3_state *state, int *count);

int	ip_fw3_basic_init(void);
int	ip_fw3_basic_fini(void);
#endif	/* _KERNEL */
#endif
