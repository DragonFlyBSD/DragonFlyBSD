/*
 * Copyright (c) 2016 The DragonFly Project.  All rights reserved.
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
 *
 */

#ifndef _IP_FW3_SYNC_H_
#define _IP_FW3_SYNC_H_

#define MAX_EDGES 10

#define SYNC_TYPE_SEND_TEST 0  /* testing sync status */
#define SYNC_TYPE_SEND_STATE 1  /* syncing state */
#define SYNC_TYPE_SEND_NAT 2  /* syncing nat */


struct ipfw_sync_edge {
	in_addr_t addr;
	u_short port;
};

struct ipfw_ioc_sync_context {
	int edge_port; /* edge listening port */
	int hw_same; /* duplicate to all CPU when hardware different */
	int count; /* count of edge */
	struct ipfw_sync_edge edges[0]; /* edge */
};

struct ipfw_ioc_sync_centre {
	int count; /* count of edge */
	struct ipfw_sync_edge edges[0]; /* edge */
};

struct ipfw_ioc_sync_edge {
	int port;
	int hw_same;
};

struct ipfw_sync_context{
	int edge_port; /* edge listening port */
	int hw_same; /* duplicate to all CPU when hardware different */
	int count; /* count of edge */
	int running; /* edge 01, centre 10 */
	struct ipfw_sync_edge *edges; /* edge */
	struct thread *edge_td; /* edge handler thread */
	struct socket *edge_sock; /* edge sock */
	struct socket *centre_socks[MAX_EDGES]; /* centre socks */
};

#ifdef _KERNEL


void ipfw3_sync_modevent(int type);

struct cmd_send_test {
	int type;
	int num;
};

struct cmd_send_state {
	int type;  /* test, state or NAT */
	struct ipfw_flow_id flow;
	uint32_t expiry;
	uint16_t lifetime;
	int rulenum;
	int cpu;
	int hash;
};

struct cmd_send_nat {
	int type; /* test, state, or NAT */
};

struct netmsg_sync {
	struct netmsg_base base;
	struct ipfw_ioc_sync_centre *centre;
	int retval;
};


typedef void ipfw_sync_install_state_t(struct cmd_send_state *cmd);

void sync_centre_conf_dispath(netmsg_t nmsg);
int ipfw_ctl_sync_centre_conf(struct sockopt *sopt);
int ipfw_ctl_sync_show_conf(struct sockopt *sopt);
int ipfw_ctl_sync_show_status(struct sockopt *sopt);
int ipfw_ctl_sync_edge_conf(struct sockopt *sopt);
void sync_edge_socket_handler(void *dummy);
int ipfw_ctl_sync_edge_start(struct sockopt *sopt);
int ipfw_ctl_sync_edge_test(struct sockopt *sopt);
int ipfw_ctl_sync_centre_start(struct sockopt *sopt);
int ipfw_ctl_sync_centre_test(struct sockopt *sopt);
int ipfw_ctl_sync_edge_stop(struct sockopt *sopt);
int ipfw_ctl_sync_centre_stop(struct sockopt *sopt);
int ipfw_ctl_sync_edge_clear(struct sockopt *sopt);
int ipfw_ctl_sync_centre_clear(struct sockopt *sopt);
int ipfw_ctl_sync_sockopt(struct sockopt *sopt);
void ipfw_sync_send_state(struct ip_fw_state *state, int cpu, int hash);

#endif /* _KERNEL */
#endif /* _IP_FW3_SYNC_H_ */
