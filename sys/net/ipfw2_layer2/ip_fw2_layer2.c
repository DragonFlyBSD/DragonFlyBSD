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

#include <net/ipfw2/ip_fw.h>

#include "ip_fw2_layer2.h"


void check_layer2(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);
void check_mac(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
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
	/* XXX TODO check the mac address */
	*cmd_val = 0;
	*cmd_ctl = IP_FW_CTL_NO;
}

static int
ipfw_layer2_init(void)
{
	register_ipfw_module(MODULE_LAYER2_ID, MODULE_LAYER2_NAME);
	register_ipfw_filter_funcs(MODULE_LAYER2_ID,
			O_LAYER2_LAYER2, (filter_func)check_layer2);
	register_ipfw_filter_funcs(MODULE_LAYER2_ID,
			O_LAYER2_MAC, (filter_func)check_mac);
	return 0;
}

static int
ipfw_layer2_stop(void)
{
	return unregister_ipfw_module(MODULE_LAYER2_ID);
}

static int
ipfw_layer2_modevent(module_t mod, int type, void *data)
{
	switch (type) {
		case MOD_LOAD:
			return ipfw_layer2_init();
		case MOD_UNLOAD:
			return ipfw_layer2_stop();
		default:
			break;
	}
	return 0;
}

static moduledata_t ipfw_layer2_mod = {
	"ipfw2_layer2",
	ipfw_layer2_modevent,
	NULL
};
DECLARE_MODULE(ipfw2_layer2, ipfw_layer2_mod, SI_SUB_PROTO_END, SI_ORDER_ANY);
MODULE_DEPEND(ipfw2_layer2, ipfw2_basic, 1, 1, 1);
MODULE_VERSION(ipfw2_layer2, 1);
