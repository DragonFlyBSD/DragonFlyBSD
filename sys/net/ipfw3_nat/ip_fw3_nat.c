/*
 * Copyright (c) 2014 - 2016 The DragonFly Project.  All rights reserved.
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

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/systimer.h>
#include <sys/thread2.h>
#include <sys/in_cksum.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/ucred.h>
#include <sys/lock.h>
#include <sys/mplock2.h>

#include <net/ethernet.h>
#include <net/netmsg2.h>
#include <net/netisr2.h>
#include <net/route.h>
#include <net/if.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/tcpip.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_var.h>
#include <netinet/ip_divert.h>

#include <net/libalias/alias.h>
#include <net/libalias/alias_local.h>

#include <net/ipfw3/ip_fw.h>

#include "ip_fw3_nat.h"


struct ipfw_nat_context	*ipfw_nat_ctx[MAXCPU];
static struct callout ipfw3_nat_cleanup_callout;
extern struct ipfw_context *ipfw_ctx[MAXCPU];
extern ip_fw_ctl_t *ipfw_ctl_nat_ptr;

static int fw3_nat_cleanup_interval = 5;

SYSCTL_NODE(_net_inet_ip, OID_AUTO, fw3_nat, CTLFLAG_RW, 0, "ipfw3 NAT");
SYSCTL_INT(_net_inet_ip_fw3_nat, OID_AUTO, cleanup_interval, CTLFLAG_RW,
		&fw3_nat_cleanup_interval, 0, "default life time");

void
check_nat(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	if ((*args)->eh != NULL) {
		*cmd_ctl = IP_FW_CTL_NO;
		*cmd_val = IP_FW_NOT_MATCH;
		return;
	}

	struct ipfw_nat_context *nat_ctx;
	struct cfg_nat *t;
	int nat_id;

	nat_ctx = ipfw_nat_ctx[mycpuid];
	(*args)->rule = *f;
	t = ((ipfw_insn_nat *)cmd)->nat;
	if (t == NULL) {
		nat_id = cmd->arg1;
		LOOKUP_NAT((*nat_ctx), nat_id, t);
		if (t == NULL) {
			*cmd_val = IP_FW_DENY;
			*cmd_ctl = IP_FW_CTL_DONE;
			return;
		}
		((ipfw_insn_nat *)cmd)->nat = t;
	}
	*cmd_val = ipfw_nat(*args, t, (*args)->m);
	*cmd_ctl = IP_FW_CTL_NAT;
}

/* Local prototypes */
u_int StartPointIn(struct in_addr, u_short, int);

u_int StartPointOut(struct in_addr, struct in_addr,
		u_short, u_short, int);

u_int
StartPointIn(struct in_addr alias_addr,
	u_short alias_port,
	int link_type)
{
	u_int n;

	n = alias_addr.s_addr;
	if (link_type != LINK_PPTP)
		n += alias_port;
	n += link_type;
	return (n % LINK_TABLE_IN_SIZE);
}


u_int
StartPointOut(struct in_addr src_addr, struct in_addr dst_addr,
	u_short src_port, u_short dst_port, int link_type)
{
	u_int n;

	n = src_addr.s_addr;
	n += dst_addr.s_addr;
	if (link_type != LINK_PPTP) {
		n += src_port;
		n += dst_port;
	}
	n += link_type;

	return (n % LINK_TABLE_OUT_SIZE);
}


void
add_alias_link_dispatch(netmsg_t alias_link_add)
{
	struct ipfw_nat_context *nat_ctx;
	struct netmsg_alias_link_add *msg;
	struct libalias *la;
	struct alias_link *lnk;
	struct cfg_nat *t;
	struct tcp_dat *aux_tcp;
	u_int start_point;

	msg = (struct netmsg_alias_link_add *)alias_link_add;
	nat_ctx = ipfw_nat_ctx[mycpuid];
	LOOKUP_NAT((*nat_ctx), msg->id, t);
	la = t->lib;
	lnk = kmalloc(sizeof(struct alias_link), M_ALIAS, M_WAITOK | M_ZERO);
	memcpy(lnk, msg->lnk, sizeof(struct alias_link));
	lnk->la = la;
	if (msg->is_tcp) {
		aux_tcp = kmalloc(sizeof(struct tcp_dat),
				M_ALIAS, M_WAITOK | M_ZERO);
		memcpy(aux_tcp, msg->lnk->data.tcp, sizeof(struct tcp_dat));
		lnk->data.tcp = aux_tcp;
	}

	/* Set up pointers for output lookup table */
	start_point = StartPointOut(lnk->src_addr, lnk->dst_addr,
			lnk->src_port, lnk->dst_port, lnk->link_type);
	LIST_INSERT_HEAD(&la->linkTableOut[start_point], lnk, list_out);

	/* Set up pointers for input lookup table */
	start_point = StartPointIn(lnk->alias_addr,
			lnk->alias_port, lnk->link_type);
	LIST_INSERT_HEAD(&la->linkTableIn[start_point], lnk, list_in);
	kfree(alias_link_add, M_LWKTMSG);
}

int
ipfw_nat(struct ip_fw_args *args, struct cfg_nat *t, struct mbuf *m)
{
	struct alias_link *new = NULL;
	struct mbuf *mcl;
	struct ip *ip;
	int ldt, retval, nextcpu;
	char *c;

	ldt = 0;
	retval = 0;
	if ((mcl = m_megapullup(m, m->m_pkthdr.len)) ==NULL)
		goto badnat;

	ip = mtod(mcl, struct ip *);
	if (args->eh == NULL) {
		ip->ip_len = htons(ip->ip_len);
		ip->ip_off = htons(ip->ip_off);
	}

	if (mcl->m_pkthdr.rcvif == NULL &&
			mcl->m_pkthdr.csum_flags & CSUM_DELAY_DATA) {
		ldt = 1;
	}

	c = mtod(mcl, char *);
	if (args->oif == NULL) {
		retval = LibAliasIn(t->lib, c,
				mcl->m_len + M_TRAILINGSPACE(mcl), &new);
	} else {
		retval = LibAliasOut(t->lib, c,
				mcl->m_len + M_TRAILINGSPACE(mcl), &new);
	}
	if (retval != PKT_ALIAS_OK &&
			retval != PKT_ALIAS_FOUND_HEADER_FRAGMENT) {
		/* XXX - should i add some logging? */
		m_free(mcl);
badnat:
		args->m = NULL;
		return IP_FW_DENY;
	}
	mcl->m_pkthdr.len = mcl->m_len = ntohs(ip->ip_len);

	if ((ip->ip_off & htons(IP_OFFMASK)) == 0 &&
			ip->ip_p == IPPROTO_TCP) {
		struct tcphdr 	*th;

		th = (struct tcphdr *)(ip + 1);
		if (th->th_x2){
			ldt = 1;
		}
	}
	if (new != NULL &&
			(ip->ip_p == IPPROTO_TCP || ip->ip_p == IPPROTO_UDP)) {
		ip_hashfn(&mcl, 0);
		nextcpu = netisr_hashcpu(m->m_pkthdr.hash);
		if (nextcpu != mycpuid) {
			struct netmsg_alias_link_add *msg;
			msg = kmalloc(sizeof(struct netmsg_alias_link_add),
					M_LWKTMSG, M_NOWAIT | M_ZERO);

			netmsg_init(&msg->base, NULL,
					&curthread->td_msgport, 0,
					add_alias_link_dispatch);
			msg->lnk = new;
			msg->id = t->id;
			if (ip->ip_p == IPPROTO_TCP) {
				msg->is_tcp = 1;
			}
			if (args->oif == NULL) {
				msg->is_outgoing = 0;
			} else {
				msg->is_outgoing = 1;
			}
			netisr_sendmsg(&msg->base, nextcpu);
		}
	}
	if (ldt) {
		struct tcphdr 	*th;
		struct udphdr 	*uh;
		u_short cksum;

		ip->ip_len = ntohs(ip->ip_len);
		cksum = in_pseudo(
				ip->ip_src.s_addr,
				ip->ip_dst.s_addr,
				htons(ip->ip_p + ip->ip_len - (ip->ip_hl << 2))
				);

		switch (ip->ip_p) {
			case IPPROTO_TCP:
				th = (struct tcphdr *)(ip + 1);
				th->th_x2 = 0;
				th->th_sum = cksum;
				mcl->m_pkthdr.csum_data =
					offsetof(struct tcphdr, th_sum);
				break;
			case IPPROTO_UDP:
				uh = (struct udphdr *)(ip + 1);
				uh->uh_sum = cksum;
				mcl->m_pkthdr.csum_data =
					offsetof(struct udphdr, uh_sum);
				break;
		}
		/*
		 * No hw checksum offloading: do it
		 * by ourself.
		 */
		if ((mcl->m_pkthdr.csum_flags & CSUM_DELAY_DATA) == 0) {
			in_delayed_cksum(mcl);
			mcl->m_pkthdr.csum_flags &= ~CSUM_DELAY_DATA;
		}
		ip->ip_len = htons(ip->ip_len);
	}

	if (args->eh == NULL) {
		ip->ip_len = ntohs(ip->ip_len);
		ip->ip_off = ntohs(ip->ip_off);
	}

	args->m = mcl;
	return IP_FW_NAT;
}

void
del_redir_spool_cfg(struct cfg_nat *n, struct redir_chain *head)
{
	struct cfg_redir *r, *tmp_r;
	struct cfg_spool *s, *tmp_s;
	int i, num;

	LIST_FOREACH_MUTABLE(r, head, _next, tmp_r) {
		num = 1; /* Number of alias_link to delete. */
		switch (r->mode) {
			case REDIR_PORT:
				num = r->pport_cnt;
				/* FALLTHROUGH */
			case REDIR_ADDR:
			case REDIR_PROTO:
				/* Delete all libalias redirect entry. */
				for (i = 0; i < num; i++)
					LibAliasRedirectDelete(n->lib,
							r->alink[i]);

				/* Del spool cfg if any. */
				LIST_FOREACH_MUTABLE(s, &r->spool_chain,
						_next, tmp_s) {
					LIST_REMOVE(s, _next);
					kfree(s, M_IPFW_NAT);
				}
				kfree(r->alink, M_IPFW_NAT);
				LIST_REMOVE(r, _next);
				kfree(r, M_IPFW_NAT);
				break;
			default:
				kprintf("unknown redirect mode: %u\n", r->mode);
				/* XXX - panic?!?!? */
				break;
		}
	}
}

int
add_redir_spool_cfg(char *buf, struct cfg_nat *ptr)
{
	struct cfg_redir *r, *ser_r;
	struct cfg_spool *s, *ser_s;
	int cnt, off, i;
	char *panic_err;

	for (cnt = 0, off = 0; cnt < ptr->redir_cnt; cnt++) {
		ser_r = (struct cfg_redir *)&buf[off];
		r = kmalloc(SOF_REDIR, M_IPFW_NAT, M_WAITOK | M_ZERO);
		memcpy(r, ser_r, SOF_REDIR);
		LIST_INIT(&r->spool_chain);
		off += SOF_REDIR;
		r->alink = kmalloc(sizeof(struct alias_link *) * r->pport_cnt,
				M_IPFW_NAT, M_WAITOK | M_ZERO);
		switch (r->mode) {
			case REDIR_ADDR:
				r->alink[0] = LibAliasRedirectAddr(ptr->lib,
							r->laddr, r->paddr);
				break;
			case REDIR_PORT:
				for (i = 0 ; i < r->pport_cnt; i++) {
					/*
					 * If remotePort is all ports
					 * set it to 0.
					 */
					u_short remotePortCopy = r->rport + i;
					if (r->rport_cnt == 1 && r->rport == 0)
						remotePortCopy = 0;
						r->alink[i] =

						LibAliasRedirectPort(ptr->lib,
						r->laddr,htons(r->lport + i),
						r->raddr,htons(remotePortCopy),
						r->paddr,htons(r->pport + i),
						r->proto);

					if (r->alink[i] == NULL) {
						r->alink[0] = NULL;
						break;
					}
				}
				break;
			case REDIR_PROTO:
				r->alink[0] = LibAliasRedirectProto(ptr->lib,
					r->laddr, r->raddr, r->paddr, r->proto);
				break;
			default:
				kprintf("unknown redirect mode: %u\n", r->mode);
				break;
		}
		if (r->alink[0] == NULL) {
			panic_err = "LibAliasRedirect* returned NULL";
			goto bad;
		} else /* LSNAT handling. */
			for (i = 0; i < r->spool_cnt; i++) {
				ser_s = (struct cfg_spool *)&buf[off];
				s = kmalloc(SOF_REDIR, M_IPFW_NAT,
						M_WAITOK | M_ZERO);
				memcpy(s, ser_s, SOF_SPOOL);
				LibAliasAddServer(ptr->lib, r->alink[0],
						s->addr, htons(s->port));
				off += SOF_SPOOL;
				/* Hook spool entry. */
				HOOK_SPOOL(&r->spool_chain, s);
			}
		/* And finally hook this redir entry. */
		HOOK_REDIR(&ptr->redir_chain, r);
	}
	return 1;
bad:
	/* something really bad happened: panic! */
	panic("%s\n", panic_err);
}

int
ipfw_ctl_nat_get_cfg(struct sockopt *sopt)
{
	struct ipfw_nat_context *nat_ctx;
	struct cfg_nat *n;
	struct cfg_redir *r;
	struct cfg_spool *s;
	int nat_cnt, off, nat_cfg_size;
	size_t size;
	uint8_t *data;

	nat_cnt = 0;
	nat_cfg_size = 0;
	off = sizeof(nat_cnt);

	nat_ctx = ipfw_nat_ctx[mycpuid];
	size = sopt->sopt_valsize;

	data = sopt->sopt_val;
	/* count the size of nat cfg */
	LIST_FOREACH(n, &((*nat_ctx).nat), _next) {
		nat_cfg_size += SOF_NAT;
	}

	LIST_FOREACH(n, &((*nat_ctx).nat), _next) {
		nat_cnt++;
		if (off + SOF_NAT < size) {
			bcopy(n, &data[off], SOF_NAT);
			off += SOF_NAT;
			LIST_FOREACH(r, &n->redir_chain, _next) {
				if (off + SOF_REDIR < size) {
					bcopy(r, &data[off], SOF_REDIR);
					off += SOF_REDIR;
					LIST_FOREACH(s, &r->spool_chain,
						_next) {
						if (off + SOF_SPOOL < size) {
							bcopy(s, &data[off],
								SOF_SPOOL);
							off += SOF_SPOOL;
						} else
							goto nospace;
					}
				} else
					goto nospace;
			}
		} else
			goto nospace;
	}
	bcopy(&nat_cnt, data, sizeof(nat_cnt));
	sopt->sopt_valsize = nat_cfg_size;
	return 0;
nospace:
	bzero(sopt->sopt_val, sopt->sopt_valsize);
	sopt->sopt_valsize = nat_cfg_size;
	return 0;
}

int
ipfw_ctl_nat_get_record(struct sockopt *sopt)
{
	struct ipfw_nat_context *nat_ctx;
	struct cfg_nat *t;
	struct alias_link *lnk;
	struct libalias *la;
	size_t sopt_size, all_lnk_size = 0;
	int i, *nat_id, id, n;
	struct ipfw_ioc_nat_state *nat_state;

	int cpu;

	nat_id = (int *)(sopt->sopt_val);
	n = *nat_id;
	sopt_size = sopt->sopt_valsize;
	nat_state = (struct ipfw_ioc_nat_state *)sopt->sopt_val;
	for (cpu = 0; cpu < ncpus; cpu++) {
		nat_ctx = ipfw_nat_ctx[cpu];
		id = n;
		LOOKUP_NAT((*nat_ctx), id, t);
		if (t != NULL) {
			la = t->lib;
			LIBALIAS_LOCK_ASSERT(la);
			for (i = 0; i < LINK_TABLE_OUT_SIZE; i++) {
				LIST_FOREACH(lnk, &la->linkTableOut[i],
						list_out) {
					all_lnk_size += sizeof(*nat_state);
					if (all_lnk_size > sopt_size)
						goto nospace;
					nat_state->src_addr = lnk->src_addr;
					nat_state->dst_addr = lnk->dst_addr;
					nat_state->alias_addr = lnk->alias_addr;
					nat_state->src_port = lnk->src_port;
					nat_state->dst_port = lnk->dst_port;
					nat_state->alias_port = lnk->alias_port;
					nat_state->link_type = lnk->link_type;
					nat_state->timestamp = lnk->timestamp;
					nat_state->cpuid = cpu;
					nat_state->is_outgoing = 1;
					nat_state++;
				}
				LIST_FOREACH(lnk, &la->linkTableIn[i],
						list_out) {
					all_lnk_size += sizeof(*nat_state);
					if (all_lnk_size > sopt_size)
						goto nospace;
					nat_state->src_addr = lnk->src_addr;
					nat_state->dst_addr = lnk->dst_addr;
					nat_state->alias_addr = lnk->alias_addr;
					nat_state->src_port = lnk->src_port;
					nat_state->dst_port = lnk->dst_port;
					nat_state->alias_port = lnk->alias_port;
					nat_state->link_type = lnk->link_type;
					nat_state->timestamp = lnk->timestamp;
					nat_state->cpuid = cpu;
					nat_state->is_outgoing = 0;
					nat_state++;
				}
			}
		}
	}
	sopt->sopt_valsize = all_lnk_size;
	return 0;
nospace:
	return 0;
}

void
nat_add_dispatch(netmsg_t nat_add_msg)
{
	struct ipfw_nat_context *nat_ctx;
	struct cfg_nat *ptr, *ser_n;
	struct netmsg_nat_add *msg;

	msg = (struct netmsg_nat_add *)nat_add_msg;

	ser_n = (struct cfg_nat *)(msg->buf);

	/* New rule: allocate and init new instance. */
	ptr = kmalloc(sizeof(struct cfg_nat), M_IPFW_NAT, M_WAITOK | M_ZERO);

	ptr->lib = LibAliasInit(NULL);
	if (ptr->lib == NULL) {
		kfree(ptr, M_IPFW_NAT);
		kfree(msg->buf, M_IPFW_NAT);
	}

	LIST_INIT(&ptr->redir_chain);
	/*
	 * Basic nat configuration.
	 */
	ptr->id = ser_n->id;
	/*
	 * XXX - what if this rule doesn't nat any ip and just
	 * redirect?
	 * do we set aliasaddress to 0.0.0.0?
	 */
	ptr->ip = ser_n->ip;
	ptr->redir_cnt = ser_n->redir_cnt;
	ptr->mode = ser_n->mode;

	LibAliasSetMode(ptr->lib, ser_n->mode, ser_n->mode);
	LibAliasSetAddress(ptr->lib, ptr->ip);
	memcpy(ptr->if_name, ser_n->if_name, IF_NAMESIZE);

	/* Add new entries. */
	add_redir_spool_cfg(&msg->buf[(sizeof(struct cfg_nat))], ptr);

	nat_ctx = ipfw_nat_ctx[mycpuid];
	HOOK_NAT(&(nat_ctx->nat), ptr);
	netisr_forwardmsg(&msg->base, mycpuid + 1);
}

int
ipfw_ctl_nat_add(struct sockopt *sopt)
{
	struct ipfw_nat_context *nat_ctx;
	struct cfg_nat *ptr, *ser_n;
	ser_n = (struct cfg_nat *)(sopt->sopt_val);

	nat_ctx = ipfw_nat_ctx[mycpuid];
	/*
	 * Find/create nat rule.
	 */
	LOOKUP_NAT((*nat_ctx), ser_n->id, ptr);

	if (ptr == NULL) {
		struct netmsg_nat_add nat_add_msg;
		struct netmsg_nat_add *msg;

		msg = &nat_add_msg;
		msg->buf = kmalloc(sopt->sopt_valsize,
				M_IPFW_NAT, M_WAITOK | M_ZERO);

		sooptcopyin(sopt, msg->buf, sopt->sopt_valsize,
				sizeof(struct cfg_nat));

		netmsg_init(&msg->base, NULL, &curthread->td_msgport,
				0, nat_add_dispatch);


		netisr_domsg(&msg->base, 0);
		kfree(msg->buf, M_IPFW_NAT);
	} else {
		goto done;
	}
done:
	return 0;
}

void
nat_del_dispatch(netmsg_t nat_del_msg)
{
	struct ipfw_nat_context *nat_ctx;
	struct ipfw_context *ctx;
	struct cfg_nat *n, *tmp;
	struct netmsg_nat_del *msg;
	struct ip_fw *f;
	ipfw_insn *cmd;
	int id;

	msg = (struct netmsg_nat_del *)nat_del_msg;
	id = msg->id;

	nat_ctx = ipfw_nat_ctx[mycpuid];
	LOOKUP_NAT((*nat_ctx), id, n);
	if (n == NULL) {
	}

	/*
	 * stop deleting when this cfg_nat was in use in ipfw_ctx
	 */
	ctx = ipfw_ctx[mycpuid];
	for (f = ctx->ipfw_rule_chain; f; f = f->next) {
		cmd = ACTION_PTR(f);
		if ((int)cmd->module == MODULE_NAT_ID &&
				(int)cmd->opcode == O_NAT_NAT) {
			tmp = ((ipfw_insn_nat *)cmd)->nat;
			if (tmp != NULL && tmp->id == n->id) {
			}
		}
	}

	UNHOOK_NAT(n);
	del_redir_spool_cfg(n, &n->redir_chain);
	LibAliasUninit(n->lib);
	kfree(n, M_IPFW_NAT);
}

int
ipfw_ctl_nat_del(struct sockopt *sopt)
{
	struct netmsg_nat_del nat_del_msg;
	struct netmsg_nat_del *msg;
	int *id;

	msg = &nat_del_msg;
	id = sopt->sopt_val;
	msg->id = *id;

	netmsg_init(&msg->base, NULL, &curthread->td_msgport,
			0, nat_del_dispatch);

	netisr_domsg(&msg->base, 0);
	return 0;
}

int
ipfw_ctl_nat_flush(struct sockopt *sopt)
{
	struct ipfw_nat_context *nat_ctx;
	struct ipfw_context *ctx;
	struct cfg_nat *ptr, *tmp;
	struct ip_fw *f;
	ipfw_insn *cmd;
	int cpu;

	/*
	 * stop flushing when any cfg_nat was in use in ipfw_ctx
	 */
	for (cpu = 0; cpu < ncpus; cpu++) {
		ctx = ipfw_ctx[cpu];
		for (f = ctx->ipfw_rule_chain; f; f = f->next) {
			cmd = ACTION_PTR(f);
			if ((int)cmd->module == MODULE_NAT_ID &&
				(int)cmd->opcode == O_NAT_NAT) {
				return EINVAL;
			}
		}
	}

	nat_ctx = ipfw_nat_ctx[mycpuid];

	LIST_FOREACH_MUTABLE(ptr, &(nat_ctx->nat), _next, tmp) {
		LIST_REMOVE(ptr, _next);
		del_redir_spool_cfg(ptr, &ptr->redir_chain);
		LibAliasUninit(ptr->lib);
		kfree(ptr, M_IPFW_NAT);
	}
	return 0;
}

int
ipfw_ctl_nat_sockopt(struct sockopt *sopt)
{
	int error = 0;
	switch (sopt->sopt_name) {
		case IP_FW_NAT_ADD:
			error = ipfw_ctl_nat_add(sopt);
			break;
		case IP_FW_NAT_DEL:
			error = ipfw_ctl_nat_del(sopt);
			break;
		case IP_FW_NAT_FLUSH:
			error = ipfw_ctl_nat_flush(sopt);
			break;
		case IP_FW_NAT_GET:
			error = ipfw_ctl_nat_get_cfg(sopt);
			break;
		case IP_FW_NAT_GET_RECORD:
			error = ipfw_ctl_nat_get_record(sopt);
			break;
		default:
			kprintf("ipfw3 nat invalid socket option %d\n",
					sopt->sopt_name);
	}
	return error;
}

void
nat_init_ctx_dispatch(netmsg_t msg)
{
	struct ipfw_nat_context *tmp;
	tmp = kmalloc(sizeof(struct ipfw_nat_context),
				M_IPFW_NAT, M_WAITOK | M_ZERO);
	ipfw_nat_ctx[mycpuid] = tmp;
	netisr_forwardmsg(&msg->base, mycpuid + 1);
}

static void
ipfw3_nat_cleanup_func_dispatch(netmsg_t nmsg)
{
	/* TODO cleanup the libalias records */
	netisr_forwardmsg(&nmsg->base, mycpuid + 1);
}

static void
ipfw3_nat_cleanup_func(void *dummy __unused)
{
	struct netmsg_base msg;
	netmsg_init(&msg, NULL, &curthread->td_msgport, 0,
			ipfw3_nat_cleanup_func_dispatch);
	netisr_domsg(&msg, 0);

	callout_reset(&ipfw3_nat_cleanup_callout,
			fw3_nat_cleanup_interval * hz,
			ipfw3_nat_cleanup_func,
			NULL);
}

static
int ipfw_nat_init(void)
{
	struct netmsg_base msg;
	register_ipfw_module(MODULE_NAT_ID, MODULE_NAT_NAME);
	register_ipfw_filter_funcs(MODULE_NAT_ID, O_NAT_NAT,
			(filter_func)check_nat);
	ipfw_ctl_nat_ptr = ipfw_ctl_nat_sockopt;
	netmsg_init(&msg, NULL, &curthread->td_msgport,
			0, nat_init_ctx_dispatch);
	netisr_domsg(&msg, 0);

	callout_init_mp(&ipfw3_nat_cleanup_callout);
	callout_reset(&ipfw3_nat_cleanup_callout,
			fw3_nat_cleanup_interval * hz,
			ipfw3_nat_cleanup_func,
			NULL);
	return 0;
}

static int
ipfw_nat_fini(void)
{
	struct cfg_nat *ptr, *tmp;
	struct ipfw_nat_context *ctx;
	int cpu;

	callout_stop(&ipfw3_nat_cleanup_callout);

	for (cpu = 0; cpu < ncpus; cpu++) {
		ctx = ipfw_nat_ctx[cpu];
		if(ctx != NULL) {
			LIST_FOREACH_MUTABLE(ptr, &(ctx->nat), _next, tmp) {
				LIST_REMOVE(ptr, _next);
				del_redir_spool_cfg(ptr, &ptr->redir_chain);
				LibAliasUninit(ptr->lib);
				kfree(ptr, M_IPFW_NAT);
			}

			kfree(ctx, M_IPFW_NAT);
			ipfw_nat_ctx[cpu] = NULL;
		}
	}
	ipfw_ctl_nat_ptr = NULL;

	return unregister_ipfw_module(MODULE_NAT_ID);
}

static int
ipfw_nat_modevent(module_t mod, int type, void *data)
{
	switch (type) {
		case MOD_LOAD:
			return ipfw_nat_init();
		case MOD_UNLOAD:
			return ipfw_nat_fini();
		default:
			break;
	}
	return 0;
}

moduledata_t ipfw_nat_mod = {
	"ipfw3_nat",
	ipfw_nat_modevent,
	NULL
};

DECLARE_MODULE(ipfw3_nat, ipfw_nat_mod,
		SI_SUB_PROTO_IFATTACHDOMAIN, SI_ORDER_ANY);
MODULE_DEPEND(ipfw3_nat, libalias, 1, 1, 1);
MODULE_DEPEND(ipfw3_nat, ipfw3_basic, 1, 1, 1);
MODULE_VERSION(ipfw3_nat, 1);
