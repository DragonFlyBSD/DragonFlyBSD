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
 *
 */

#ifndef _IP_FW3_TABLE_H_
#define _IP_FW3_TABLE_H_

#define IPFW_TABLES_MAX		32
#define IPFW_TABLE_NAME_LEN	32

#ifdef _KERNEL
struct ipfw_table_context {
	struct	radix_node_head *node;
	struct	radix_node_head *mask;
	char	name[IPFW_TABLE_NAME_LEN];
	int	count;
	int 	type;
};

struct table_ip_entry{
	struct radix_node	rn[2];
	struct sockaddr_in	addr;
	struct sockaddr_in	mask;
};

struct table_mac_entry {
	struct radix_node	rn[2];
        struct sockaddr         addr;
        struct sockaddr         mask;
};

struct netmsg_table {
	struct netmsg_base base;
	struct ipfw_ioc_table *ioc_table;
	int retval;
};

int ipfw_ctl_table_list(struct sockopt *sopt);
int ipfw_ctl_table_remove(struct sockopt *sopt);
int ipfw_ctl_table_flush(struct sockopt *sopt);
int flush_table_ip_entry(struct radix_node *rn, void *arg);
int flush_table_mac_entry(struct radix_node *rn, void *arg);
int dump_table_ip_entry(struct radix_node *rn, void *arg);
int dump_table_mac_entry(struct radix_node *rn, void *arg);
int ipfw_ctl_table_show(struct sockopt *sopt);
int ipfw_ctl_table_test(struct sockopt *sopt);
int ipfw_ctl_table_rename(struct sockopt *sopt);
int ipfw_ctl_table_create(struct sockopt *sopt);
int ipfw_ctl_table_delete(struct sockopt *sopt);
int ipfw_ctl_table_append(struct sockopt *sopt);
int ipfw_ctl_table_sockopt(struct sockopt *sopt);

void table_init_dispatch(netmsg_t nmsg);
void table_fini(void);

#endif	/* _KERNEL */

struct ipfw_ioc_table_ip_entry {
	in_addr_t	addr;		/* network address */
	u_int8_t	masklen;	/* mask length */
};

struct ipfw_ioc_table_mac_entry {
        struct ether_addr       addr;
};

struct ipfw_ioc_table {
	int	id;
	int 	type;
	int	count;
	char	name[IPFW_TABLE_NAME_LEN];
	struct ipfw_ioc_table_ip_entry ip_ent[0];
	struct ipfw_ioc_table_mac_entry mac_ent[0];
};

#endif /* _IP_FW3_TABLE_H_ */
