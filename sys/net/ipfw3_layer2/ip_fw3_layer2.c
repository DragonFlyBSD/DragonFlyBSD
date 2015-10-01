/*
 * Copyright (c) 2014 The DragonFly Project.  All rights reserved.
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

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/systimer.h>
#include <sys/thread2.h>

#include <net/ethernet.h>
#include <net/netmsg2.h>
#include <net/netisr2.h>
#include <net/route.h>

#include <netinet/in_var.h>
#include <netinet/ip_var.h>

#include <net/ipfw3/ip_fw.h>
#include <net/ipfw3/ip_fw3_table.h>

#include "ip_fw3_layer2.h"

extern struct ipfw_context      *ipfw_ctx[MAXCPU];

void check_layer2(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void check_mac(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void
check_mac_from(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void
check_mac_from_lookup(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void
check_mac_to(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void
check_mac_to_lookup(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);

void
check_layer2(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	*cmd_val = ((*args)->eh != NULL);
	*cmd_ctl = IP_FW_CTL_NO;
}

void
check_mac(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	*cmd_ctl = IP_FW_CTL_NO;
	if ((*args)->eh != NULL) {
		uint32_t *want = (uint32_t *)((ipfw_insn_mac *)cmd)->addr;
		uint32_t *mask = (uint32_t *)((ipfw_insn_mac *)cmd)->mask;
		uint32_t *hdr = (uint32_t *)(*args)->eh;
		*cmd_val =
			(want[0] == (hdr[0] & mask[0]) &&
			 want[1] == (hdr[1] & mask[1]) &&
			 want[2] == (hdr[2] & mask[2]));
	} else {
		*cmd_val = IP_FW_NOT_MATCH;
	}
}

void
check_mac_from(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	*cmd_ctl = IP_FW_CTL_NO;
	if ((*args)->eh != NULL) {
		uint16_t *want = (uint16_t *)((ipfw_insn_mac *)cmd)->addr;
		uint16_t *mask = (uint16_t *)((ipfw_insn_mac *)cmd)->mask;
		uint16_t *hdr = (uint16_t *)(*args)->eh;
		*cmd_val =
			(want[3] == (hdr[3] & mask[3]) &&
			 want[4] == (hdr[4] & mask[4]) &&
			 want[5] == (hdr[5] & mask[5]));
	} else {
		*cmd_val = IP_FW_NOT_MATCH;
	}
}

void
check_mac_from_lookup(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
        struct ipfw_context *ctx = ipfw_ctx[mycpuid];
        struct ipfw_table_context *table_ctx;
        struct radix_node_head *rnh;
        struct table_mac_entry *ent = NULL;

        table_ctx = ctx->table_ctx;
        table_ctx += cmd->arg1;
        rnh = table_ctx->node;

        *cmd_ctl = IP_FW_CTL_NO;
        *cmd_val = IP_FW_NOT_MATCH;
        if ((*args)->eh != NULL) {
                struct sockaddr sa;
                sa.sa_len = 8;
                strncpy(sa.sa_data, (*args)->eh->ether_shost, 6);
                ent = (struct table_mac_entry *)rnh->rnh_lookup((char *)&sa,
								NULL, rnh);
                if(ent != NULL)
                        *cmd_val = IP_FW_MATCH;
        }
}

void
check_mac_to(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	*cmd_ctl = IP_FW_CTL_NO;
	if ((*args)->eh != NULL) {
		uint16_t *want = (uint16_t *)((ipfw_insn_mac *)cmd)->addr;
		uint16_t *mask = (uint16_t *)((ipfw_insn_mac *)cmd)->mask;
		uint16_t *hdr = (uint16_t *)(*args)->eh;
		*cmd_val =
			(want[0] == (hdr[0] & mask[0]) &&
			 want[1] == (hdr[1] & mask[1]) &&
			 want[2] == (hdr[2] & mask[2]));
	} else {
		*cmd_val = IP_FW_NOT_MATCH;
	}
}

void
check_mac_to_lookup(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
        struct ipfw_context *ctx = ipfw_ctx[mycpuid];
        struct ipfw_table_context *table_ctx;
        struct radix_node_head *rnh;
        struct table_mac_entry *ent = NULL;

        table_ctx = ctx->table_ctx;
        table_ctx += cmd->arg1;
        rnh = table_ctx->node;

        *cmd_ctl = IP_FW_CTL_NO;
        *cmd_val = IP_FW_NOT_MATCH;
        if ((*args)->eh != NULL) {
                struct sockaddr sa;
                sa.sa_len = 8;
                strncpy(sa.sa_data, (*args)->eh->ether_dhost, 6);
                ent = (struct table_mac_entry *)rnh->rnh_lookup((char *)&sa,
								NULL, rnh);
                if(ent != NULL)
                        *cmd_val = IP_FW_MATCH;
        }
}

static int
ipfw3_layer2_init(void)
{
	register_ipfw_module(MODULE_LAYER2_ID, MODULE_LAYER2_NAME);
	register_ipfw_filter_funcs(MODULE_LAYER2_ID,
			O_LAYER2_LAYER2, (filter_func)check_layer2);
	register_ipfw_filter_funcs(MODULE_LAYER2_ID,
			O_LAYER2_MAC, (filter_func)check_mac);
        register_ipfw_filter_funcs(MODULE_LAYER2_ID,
			O_LAYER2_MAC_SRC, (filter_func)check_mac_from);
        register_ipfw_filter_funcs(MODULE_LAYER2_ID,
			O_LAYER2_MAC_DST, (filter_func)check_mac_to);
        register_ipfw_filter_funcs(MODULE_LAYER2_ID,
			O_LAYER2_MAC_SRC_LOOKUP,
			(filter_func)check_mac_from_lookup);
        register_ipfw_filter_funcs(MODULE_LAYER2_ID,
			O_LAYER2_MAC_DST_LOOKUP,
			(filter_func)check_mac_to_lookup);
	return 0;
}

static int
ipfw3_layer2_stop(void)
{
	return unregister_ipfw_module(MODULE_LAYER2_ID);
}

static int
ipfw3_layer2_modevent(module_t mod, int type, void *data)
{
	switch (type) {
		case MOD_LOAD:
			return ipfw3_layer2_init();
		case MOD_UNLOAD:
			return ipfw3_layer2_stop();
		default:
			break;
	}
	return 0;
}

static moduledata_t ipfw3_layer2_mod = {
	"ipfw3_layer2",
	ipfw3_layer2_modevent,
	NULL
};
DECLARE_MODULE(ipfw3_layer2, ipfw3_layer2_mod, SI_SUB_PROTO_END, SI_ORDER_ANY);
MODULE_DEPEND(ipfw3_layer2, ipfw3_basic, 1, 1, 1);
MODULE_VERSION(ipfw3_layer2, 1);
