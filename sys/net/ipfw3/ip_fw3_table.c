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

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/ucred.h>
#include <sys/in_cksum.h>
#include <sys/lock.h>
#include <sys/thread2.h>
#include <sys/mplock2.h>

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
#include <netinet/ip_divert.h>
#include <netinet/if_ether.h>

#include <net/if.h>
#include <net/route.h>
#include <net/pfil.h>
#include <net/netmsg2.h>
#include <net/ethernet.h>

#include <net/ipfw3/ip_fw.h>
#include <net/ipfw3/ip_fw3_table.h>

MALLOC_DEFINE(M_IPFW3_TABLE, "IPFW3_TABLE", "mem for ip_fw3 table");

extern struct ipfw_context	*ipfw_ctx[MAXCPU];

/*
 * activate/create the table by setup the type and reset counts.
 */
static void
table_create_dispatch(netmsg_t nmsg)
{
	struct netmsg_table *tbmsg = (struct netmsg_table *)nmsg;
	struct ipfw_ioc_table *ioc_table;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ipfw_table_context *table_ctx;
	ioc_table = tbmsg->ioc_table;
	int id = ioc_table->id;

	table_ctx = ctx->table_ctx;
	table_ctx += id;
	table_ctx->type = ioc_table->type;
	table_ctx->count = 0;
	strlcpy(table_ctx->name , ioc_table->name, IPFW_TABLE_NAME_LEN);
	if (table_ctx->type == 1) {
		rn_inithead((void **)&table_ctx->mask, NULL, 0);
		rn_inithead((void **)&table_ctx->node, table_ctx->mask, 32);
        } else if (table_ctx->type == 2) {
                rn_inithead((void **)&table_ctx->mask, NULL, 0);
		rn_inithead((void **)&table_ctx->node, table_ctx->mask, 48);
        } else {
                goto done;
        }
done:
	ifnet_forwardmsg(&nmsg->lmsg, mycpuid + 1);
}

/*
 * clean the table, especially the node
 */
static void
table_delete_dispatch(netmsg_t nmsg)
{
	struct netmsg_table *tbmsg = (struct netmsg_table *)nmsg;
	struct ipfw_ioc_table *ioc_tbl;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ipfw_table_context *table_ctx;
	struct radix_node_head *rnh;

	ioc_tbl = tbmsg->ioc_table;
	table_ctx = ctx->table_ctx;
	table_ctx += ioc_tbl->id;
	table_ctx->count = 0;

        if (table_ctx->type == 1) {
                rnh = table_ctx->node;
                rnh->rnh_walktree(rnh, flush_table_ip_entry, rnh);
        } else if (table_ctx->type == 2) {
                rnh = table_ctx->node;
                rnh->rnh_walktree(rnh, flush_table_mac_entry, rnh);
        }
	table_ctx->type = 0;
	ifnet_forwardmsg(&nmsg->lmsg, mycpuid + 1);
}

static void
table_append_dispatch(netmsg_t nmsg)
{
	struct netmsg_table *tbmsg = (struct netmsg_table *)nmsg;
	struct ipfw_ioc_table *ioc_tbl;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ipfw_table_context *table_ctx;
	struct radix_node_head *rnh;

	uint8_t mlen;

	ioc_tbl = tbmsg->ioc_table;
	table_ctx = ctx->table_ctx;
	table_ctx += ioc_tbl->id;
	if (table_ctx->type != ioc_tbl->type)
		goto done;

        if (table_ctx->type == 1 ) {
                struct table_ip_entry *ent;

                rnh = table_ctx->node;
                ent = kmalloc(sizeof(struct table_ip_entry),
                                M_IPFW3_TABLE, M_NOWAIT | M_ZERO);

                if (ent == NULL)
                        return;
                mlen = ioc_tbl->ip_ent->masklen;
                ent->addr.sin_len = ent->mask.sin_len = 8;
                ent->mask.sin_addr.s_addr = htonl(~((1 << (32 - mlen)) - 1));
                ent->addr.sin_addr.s_addr = ioc_tbl->ip_ent->addr &
                                                ent->mask.sin_addr.s_addr;

                if (rnh->rnh_addaddr((char *)&ent->addr,
                                (char *)&ent->mask, rnh,
                                (void *)ent->rn) != NULL) {
                        table_ctx->count++;
                }
        } else if (table_ctx->type == 2 ) {
                struct table_mac_entry *ent;

                rnh = table_ctx->node;
                ent = kmalloc(sizeof(struct table_mac_entry),
                                M_IPFW3_TABLE, M_NOWAIT | M_ZERO);
                if (ent == NULL)
                        return;
                ent->addr.sa_len = 8;
                strncpy(ent->addr.sa_data, ioc_tbl->mac_ent->addr.octet, 6);

                if (rnh->rnh_addaddr((char *)&ent->addr,
                                NULL, rnh, (void *)ent->rn) != NULL) {
                       table_ctx->count++;
                }
        }

done:
	ifnet_forwardmsg(&nmsg->lmsg, mycpuid + 1);
}

static void
table_remove_dispatch(netmsg_t nmsg)
{
	struct netmsg_table *tbmsg = (struct netmsg_table *)nmsg;
	struct ipfw_ioc_table *ioc_tbl;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ipfw_table_context *table_ctx;
	struct radix_node_head *rnh;
	struct table_entry *ent;
	struct sockaddr_in sa, mask;
	in_addr_t addr;
	uint8_t mlen;

	ioc_tbl = tbmsg->ioc_table;
	table_ctx = ctx->table_ctx;
	table_ctx += ioc_tbl->id;
	if (table_ctx->type != ioc_tbl->type)
		goto done;

	rnh = table_ctx->node;

	mlen = ioc_tbl->ip_ent->masklen;
	addr = ioc_tbl->ip_ent->addr;

	sa.sin_len = mask.sin_len = 8;
	mask.sin_addr.s_addr = htonl(mlen ? ~((1 << (32 - mlen)) - 1) : 0);
	sa.sin_addr.s_addr = addr & mask.sin_addr.s_addr;

	ent = (struct table_entry *)rnh->rnh_deladdr((char *)&sa, (char *)&mask, rnh);
	if (ent != NULL) {
		table_ctx->count--;
		kfree(ent, M_IPFW3_TABLE);
	}
done:
	ifnet_forwardmsg(&nmsg->lmsg, mycpuid + 1);
}

int
flush_table_ip_entry(struct radix_node *rn, void *arg)
{
	struct radix_node_head *rnh = arg;
	struct table_ip_entry *ent;

	ent = (struct table_ip_entry *)
		rnh->rnh_deladdr(rn->rn_key, rn->rn_mask, rnh);
	if (ent != NULL)
		kfree(ent, M_IPFW3_TABLE);
	return (0);
}

int
flush_table_mac_entry(struct radix_node *rn, void *arg)
{
	struct radix_node_head *rnh = arg;
	struct table_mac_entry *ent;

	ent = (struct table_mac_entry *)
		rnh->rnh_deladdr(rn->rn_key, rn->rn_mask, rnh);
	if (ent != NULL)
		kfree(ent, M_IPFW3_TABLE);
	return (0);
}

static void
table_flush_dispatch(netmsg_t nmsg)
{
	struct netmsg_table *tbmsg = (struct netmsg_table *)nmsg;
	struct ipfw_ioc_table *ioc_tbl;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ipfw_table_context *table_ctx;
	struct radix_node_head *rnh;

	ioc_tbl = tbmsg->ioc_table;
	table_ctx = ctx->table_ctx;
	table_ctx += ioc_tbl->id;
	rnh = table_ctx->node;
	table_ctx->count = 0;

	rnh->rnh_walktree(rnh, flush_table_ip_entry, rnh);
	ifnet_forwardmsg(&nmsg->lmsg, mycpuid + 1);
}

/*
 * rename the table
 */
static void
table_rename_dispatch(netmsg_t nmsg)
{
	struct netmsg_table *tbmsg = (struct netmsg_table *)nmsg;
	struct ipfw_ioc_table *ioc_tbl;
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ipfw_table_context *table_ctx;

	ioc_tbl = tbmsg->ioc_table;
	table_ctx = ctx->table_ctx;
	table_ctx += ioc_tbl->id;
	strlcpy(table_ctx->name, ioc_tbl->name, IPFW_TABLE_NAME_LEN);
	ifnet_forwardmsg(&nmsg->lmsg, mycpuid + 1);
}

/*
 * list all the overview information about each table
 */
int
ipfw_ctl_table_list(struct sockopt *sopt)
{
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ipfw_table_context *table_ctx = ctx->table_ctx;
	struct ipfw_ioc_table *ioc_table;
	int i, error = 0, size;

	size = IPFW_TABLES_MAX * sizeof(struct ipfw_ioc_table);
	if (sopt->sopt_valsize < size) {
		/* sopt_val is not big enough */
		bzero(sopt->sopt_val, sopt->sopt_valsize);
		return 0;
	}
	ioc_table = (struct ipfw_ioc_table *)sopt->sopt_val;
	for (i = 0; i < IPFW_TABLES_MAX; i++, ioc_table++, table_ctx++) {
		ioc_table->id = i;
		ioc_table->type = table_ctx->type;
		ioc_table->count = table_ctx->count;
		strlcpy(ioc_table->name, table_ctx->name, IPFW_TABLE_NAME_LEN);
	}
	sopt->sopt_valsize = size;
	return error;
}

/*
 * remove an item from the table
 */
int
ipfw_ctl_table_remove(struct sockopt *sopt)
{
	struct netmsg_table tbmsg;
	bzero(&tbmsg,sizeof(tbmsg));
	tbmsg.ioc_table = sopt->sopt_val;
	netmsg_init(&tbmsg.base, NULL, &curthread->td_msgport,
			0, table_remove_dispatch);
	ifnet_domsg(&tbmsg.base.lmsg, 0);
	return tbmsg.retval;
}

/*
 * flush everything inside the table
 */
int
ipfw_ctl_table_flush(struct sockopt *sopt)
{
	struct netmsg_table tbmsg;
	bzero(&tbmsg,sizeof(tbmsg));
	tbmsg.ioc_table = sopt->sopt_val;
	netmsg_init(&tbmsg.base, NULL, &curthread->td_msgport,
			0, table_flush_dispatch);
	ifnet_domsg(&tbmsg.base.lmsg, 0);
	return tbmsg.retval;
}

/*
 * dump the entries into the ioc_table
 */
int
dump_table_ip_entry(struct radix_node *rn, void *arg)
{
	struct table_ip_entry *ent = (struct table_ip_entry *)rn;
	struct ipfw_ioc_table_ip_entry *ioc_ent;
	struct ipfw_ioc_table *tbl = arg;
        struct sockaddr_in *addr, *mask;

        addr = &ent->addr;
        mask = &ent->mask;

	ioc_ent = &tbl->ip_ent[tbl->count];
	if (in_nullhost(mask->sin_addr))
		ioc_ent->masklen = 0;
	else
		ioc_ent->masklen = 33 - ffs(ntohl(mask->sin_addr.s_addr));
	ioc_ent->addr = addr->sin_addr.s_addr;
	tbl->count++;
	return (0);
}

int
dump_table_mac_entry(struct radix_node *rn, void *arg)
{
	struct table_mac_entry *ent = (struct table_mac_entry *)rn;
	struct ipfw_ioc_table_mac_entry *ioc_ent;
	struct ipfw_ioc_table *tbl = arg;
	ioc_ent = &tbl->mac_ent[tbl->count];
        strncpy(ioc_ent->addr.octet, ent->addr.sa_data, 6);
	tbl->count++;
	return (0);
}

/*
 * get and display all items in the table
 */
int
ipfw_ctl_table_show(struct sockopt *sopt)
{
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ipfw_table_context *table_ctx;
	struct radix_node_head *rnh;
	struct ipfw_ioc_table *tbl;
	void *data;
	int size;

	int *id = (int *)sopt->sopt_val;
	table_ctx = ctx->table_ctx;
	table_ctx += *id;
        if (table_ctx->type == 1) {
                size = table_ctx->count * sizeof(struct ipfw_ioc_table_ip_entry) +
                                sizeof(struct ipfw_ioc_table);
                if (sopt->sopt_valsize < size) {
                        /* sopt_val is not big enough */
                        bzero(sopt->sopt_val, sopt->sopt_valsize);
                        return 0;
                }
                data = kmalloc(size, M_IPFW3_TABLE, M_NOWAIT | M_ZERO);
                tbl = (struct ipfw_ioc_table *)data;
                tbl->id = *id;
                tbl->type = table_ctx->type;
		strlcpy(tbl->name, table_ctx->name, IPFW_TABLE_NAME_LEN);
                rnh = table_ctx->node;
                rnh->rnh_walktree(rnh, dump_table_ip_entry, tbl);
                bcopy(tbl, sopt->sopt_val, size);
                sopt->sopt_valsize = size;
                kfree(data, M_IPFW3_TABLE);
        } else if (table_ctx->type == 2) {
                size = table_ctx->count * sizeof(struct ipfw_ioc_table_mac_entry) +
                                sizeof(struct ipfw_ioc_table);
                if (sopt->sopt_valsize < size) {
                        /* sopt_val is not big enough */
                        bzero(sopt->sopt_val, sopt->sopt_valsize);
                        return 0;
                }
                data = kmalloc(size, M_IPFW3_TABLE, M_NOWAIT | M_ZERO);
                tbl = (struct ipfw_ioc_table *)data;
                tbl->id = *id;
                tbl->type = table_ctx->type;
		strlcpy(tbl->name, table_ctx->name, IPFW_TABLE_NAME_LEN);
                rnh = table_ctx->node;
                rnh->rnh_walktree(rnh, dump_table_mac_entry, tbl);
                bcopy(tbl, sopt->sopt_val, size);
                sopt->sopt_valsize = size;
                kfree(data, M_IPFW3_TABLE);
        }
	return 0;
}

/*
 * test whether the ip is in the table
 */
int
ipfw_ctl_table_test(struct sockopt *sopt)
{
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	struct ipfw_table_context *table_ctx;
	struct radix_node_head *rnh;
	struct ipfw_ioc_table *tbl;

	tbl = (struct ipfw_ioc_table *)sopt->sopt_val;
	table_ctx = ctx->table_ctx;
	table_ctx += tbl->id;

        if (table_ctx->type != tbl->type)
                goto done;

        rnh = table_ctx->node;
        if (tbl->type == 1) {
                struct sockaddr_in sa;
                sa.sin_len = 8;
                sa.sin_addr.s_addr = tbl->ip_ent->addr;

                if(rnh->rnh_lookup((char *)&sa, NULL, rnh) != NULL)
                        return 0;
        } else if (tbl->type == 2) {
                struct sockaddr sa;
                sa.sa_len = 8;
                strncpy(sa.sa_data, tbl->mac_ent->addr.octet, 6);

                if(rnh->rnh_lookup((char *)&sa, NULL, rnh) != NULL)
                        return 0;
        } else {
                /* XXX TODO */
        }
done:
	return 1;
}

/*
 * activate the table
 */
int
ipfw_ctl_table_create(struct sockopt *sopt)
{
	struct netmsg_table tbmsg;
	bzero(&tbmsg,sizeof(tbmsg));
	tbmsg.ioc_table = sopt->sopt_val;
	netmsg_init(&tbmsg.base, NULL, &curthread->td_msgport,
			0, table_create_dispatch);
	ifnet_domsg(&tbmsg.base.lmsg, 0);
	return tbmsg.retval;
}

/*
 * deactivate the table
 */
int
ipfw_ctl_table_delete(struct sockopt *sopt)
{
	struct netmsg_table tbmsg;
	bzero(&tbmsg,sizeof(tbmsg));
	tbmsg.ioc_table = sopt->sopt_val;
	netmsg_init(&tbmsg.base, NULL, &curthread->td_msgport,
			0, table_delete_dispatch);
	ifnet_domsg(&tbmsg.base.lmsg, 0);
	return tbmsg.retval;
}

/*
 * append an item into the table
 */
int
ipfw_ctl_table_append(struct sockopt *sopt)
{
	struct netmsg_table tbmsg;
	bzero(&tbmsg,sizeof(tbmsg));
	tbmsg.ioc_table = sopt->sopt_val;
	netmsg_init(&tbmsg.base, NULL, &curthread->td_msgport,
			0, table_append_dispatch);
	ifnet_domsg(&tbmsg.base.lmsg, 0);
	return tbmsg.retval;
}

/*
 * rename an table
 */
int
ipfw_ctl_table_rename(struct sockopt *sopt)
{
	struct netmsg_table tbmsg;
	bzero(&tbmsg,sizeof(tbmsg));
	tbmsg.ioc_table = sopt->sopt_val;
	netmsg_init(&tbmsg.base, NULL, &curthread->td_msgport,
			0, table_rename_dispatch);
	ifnet_domsg(&tbmsg.base.lmsg, 0);
	return tbmsg.retval;
}

/*
 * sockopt handler
 */
int
ipfw_ctl_table_sockopt(struct sockopt *sopt)
{
	int error = 0;
	switch (sopt->sopt_name) {
		case IP_FW_TABLE_CREATE:
			error = ipfw_ctl_table_create(sopt);
			break;
		case IP_FW_TABLE_DELETE:
			error = ipfw_ctl_table_delete(sopt);
			break;
		case IP_FW_TABLE_APPEND:
			error = ipfw_ctl_table_append(sopt);
			break;
		case IP_FW_TABLE_REMOVE:
			error = ipfw_ctl_table_remove(sopt);
			break;
		case IP_FW_TABLE_LIST:
			error = ipfw_ctl_table_list(sopt);
			break;
		case IP_FW_TABLE_FLUSH:
			error = ipfw_ctl_table_flush(sopt);
			break;
		case IP_FW_TABLE_SHOW:
			error = ipfw_ctl_table_show(sopt);
			break;
		case IP_FW_TABLE_TEST:
			error = ipfw_ctl_table_test(sopt);
			break;
		case IP_FW_TABLE_RENAME:
			error = ipfw_ctl_table_rename(sopt);
			break;
		default:
			kprintf("ipfw table invalid socket option %d\n",
				sopt->sopt_name);
	}
	return error;
}

static void
table_init_ctx_dispatch(netmsg_t nmsg)
{
	struct ipfw_context *ctx = ipfw_ctx[mycpuid];
	ctx->table_ctx = kmalloc(sizeof(struct ipfw_table_context) * IPFW_TABLES_MAX,
			M_IPFW3_TABLE, M_WAITOK | M_ZERO);
	ifnet_forwardmsg(&nmsg->lmsg, mycpuid + 1);
}

/*
 * release the memory of the tables
 */
void
table_fini(void)
{
	struct ipfw_table_context *table_ctx, *tmp_table;
	struct radix_node_head *rnh;
	int cpu, id;
	for (cpu = 0; cpu < ncpus; cpu++) {
		table_ctx = ipfw_ctx[cpu]->table_ctx;
		tmp_table = table_ctx;
		for (id = 0; id < IPFW_TABLES_MAX; id++, table_ctx++) {
			if (table_ctx->type == 1) {
				rnh = table_ctx->node;
				rnh->rnh_walktree(rnh, flush_table_ip_entry, rnh);
			} else if (table_ctx->type == 2) {
				rnh = table_ctx->node;
				rnh->rnh_walktree(rnh, flush_table_mac_entry, rnh);
			}
		}
		kfree(tmp_table, M_IPFW3_TABLE);
	}
}

/*
 * it will be invoked during init of ipfw3
 * this function will prepare the tables
 */
void
table_init_dispatch(netmsg_t nmsg)
{
	int error = 0;
	struct netmsg_base nmsg_base;
	bzero(&nmsg_base, sizeof(nmsg_base));
	netmsg_init(&nmsg_base, NULL, &curthread->td_msgport,
			0, table_init_ctx_dispatch);
	ifnet_domsg(&nmsg_base.lmsg, 0);
	lwkt_replymsg(&nmsg->lmsg, error);
}
