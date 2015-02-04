/*
 * Copyright (c) 2015 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Bill Yuan <bycn82@gmail.com>
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

#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/systimer.h>
#include <sys/param.h>
#include <sys/ucred.h>

#include <netinet/in_var.h>
#include <netinet/ip_var.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/in_pcb.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/tcpip.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/if_ether.h>

#include <net/ethernet.h>
#include <net/netmsg2.h>
#include <net/netisr2.h>
#include <net/route.h>

#include <net/ipfw2/ip_fw2.h>

#include "ip_fw_layer4.h"

void
check_tcpflag(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void
check_uid(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void
check_gid(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);

/*
 * ipfw_match_guid can match the gui and uid
 */
static int
ipfw_match_guid(const struct ipfw_flow_id *fid, struct ifnet *oif,
		int opcode, uid_t uid)
{
	struct in_addr src_ip, dst_ip;
	struct inpcbinfo *pi;
	boolean_t wildcard;
	struct inpcb *pcb;

	if (fid->proto == IPPROTO_TCP) {
		wildcard = FALSE;
		pi = &tcbinfo[mycpuid];
	} else if (fid->proto == IPPROTO_UDP) {
		wildcard = TRUE;
		pi = &udbinfo[mycpuid];
	} else {
		return 0;
	}

	/*
	 * Values in 'fid' are in host byte order
	 */
	dst_ip.s_addr = htonl(fid->dst_ip);
	src_ip.s_addr = htonl(fid->src_ip);
	if (oif) {
		pcb = in_pcblookup_hash(pi,
				dst_ip, htons(fid->dst_port),
				src_ip, htons(fid->src_port),
				wildcard, oif);
	} else {
		pcb = in_pcblookup_hash(pi,
				src_ip, htons(fid->src_port),
				dst_ip, htons(fid->dst_port),
				wildcard, NULL);
	}
	if (pcb == NULL || pcb->inp_socket == NULL) {
		return 0;
	}

	if (opcode == O_LAYER4_UID) {
#define socheckuid(a,b)	((a)->so_cred->cr_uid != (b))
		return !socheckuid(pcb->inp_socket, uid);
#undef socheckuid
	} else  {
		return groupmember(uid, pcb->inp_socket->so_cred);
	}
}

void
check_tcpflag(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	/* XXX TODO check tcpflag */
	*cmd_val = 0;
	*cmd_ctl = IP_FW_CTL_NO;
}

void
check_uid(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	*cmd_val = ipfw_match_guid(&(*args)->f_id, (*args)->oif, cmd->opcode,
				(uid_t)((ipfw_insn_u32 *)cmd)->d[0]);
	*cmd_ctl = IP_FW_CTL_NO;
}

void
check_gid(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	*cmd_val = ipfw_match_guid(&(*args)->f_id, (*args)->oif, cmd->opcode,
				(gid_t)((ipfw_insn_u32 *)cmd)->d[0]);
	*cmd_ctl = IP_FW_CTL_NO;
}

static int
ipfw_layer4_init(void)
{
	register_ipfw_module(MODULE_LAYER4_ID, MODULE_LAYER4_NAME);
	register_ipfw_filter_funcs(MODULE_LAYER4_ID, O_LAYER4_TCPFLAG,
			(filter_func)check_tcpflag);
	register_ipfw_filter_funcs(MODULE_LAYER4_ID, O_LAYER4_UID,
			(filter_func)check_uid);
	register_ipfw_filter_funcs(MODULE_LAYER4_ID, O_LAYER4_GID,
			(filter_func)check_gid);
	return 0;
}

static int
ipfw_layer4_stop(void)
{
	return unregister_ipfw_module(MODULE_LAYER4_ID);
}

static int
ipfw_layer4_modevent(module_t mod, int type, void *data)
{
	switch (type) {
	case MOD_LOAD:
		return ipfw_layer4_init();
	case MOD_UNLOAD:
		return ipfw_layer4_stop();
	default:
		break;
	}
	return 0;
}

static moduledata_t ipfw_layer4_mod = {
	"ipfw_layer4",
	ipfw_layer4_modevent,
	NULL
};
DECLARE_MODULE(ipfw_layer4, ipfw_layer4_mod, SI_SUB_PROTO_END, SI_ORDER_ANY);
MODULE_DEPEND(ipfw_layer4, ipfw_basic, 1, 1, 1);
MODULE_VERSION(ipfw_layer4, 1);
