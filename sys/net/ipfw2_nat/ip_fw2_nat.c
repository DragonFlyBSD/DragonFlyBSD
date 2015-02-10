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

#include <net/ipfw2/ip_fw.h>

#include "ip_fw2_nat.h"


static struct lock nat_lock;

extern struct ipfw_nat_context *ipfw_nat_ctx;
extern ipfw_nat_cfg_t *ipfw_nat_cfg_ptr;
extern ipfw_nat_cfg_t *ipfw_nat_del_ptr;
extern ipfw_nat_cfg_t *ipfw_nat_flush_ptr;
extern ipfw_nat_cfg_t *ipfw_nat_get_cfg_ptr;
extern ipfw_nat_cfg_t *ipfw_nat_get_log_ptr;

typedef int ipfw_nat_t(struct ip_fw_args *, struct cfg_nat *, struct mbuf *);

int ipfw_nat(struct ip_fw_args *args, struct cfg_nat *t, struct mbuf *m);
int ipfw_nat_cfg(struct sockopt *sopt);
int ipfw_nat_del(struct sockopt *sopt);
int ipfw_nat_flush(struct sockopt *sopt);
void check_nat(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len);


void
check_nat(int *cmd_ctl, int *cmd_val, struct ip_fw_args **args,
		struct ip_fw **f, ipfw_insn *cmd, uint16_t ip_len)
{
	if ((*args)->eh != NULL) {
		*cmd_ctl = IP_FW_CTL_NO;
		*cmd_val = IP_FW_NOT_MATCH;
		return;
	}
	struct cfg_nat *t;
	int nat_id;
	(*args)->rule = *f;
	lockmgr(&nat_lock, LK_SHARED);
	t = ((ipfw_insn_nat *)cmd)->nat;
	if (t == NULL) {
		nat_id = cmd->arg1;
		LOOKUP_NAT((*ipfw_nat_ctx), nat_id, t);
		if (t == NULL) {
			*cmd_val = IP_FW_DENY;
			*cmd_ctl = IP_FW_CTL_DONE;
			return;
		}
		((ipfw_insn_nat *)cmd)->nat = t;
	}
	*cmd_val = ipfw_nat(*args, t, (*args)->m);
	lockmgr(&nat_lock, LK_RELEASE);
	*cmd_ctl = IP_FW_CTL_NAT;
}

static void
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
				for (i = 0; i < num; i++) {
					LibAliasRedirectDelete(n->lib, r->alink[i]);
				}
				/* Del spool cfg if any. */
				LIST_FOREACH_MUTABLE(s, &r->spool_chain, _next, tmp_s) {
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

static int
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
					/* If remotePort is all ports, set it to 0. */
					u_short remotePortCopy = r->rport + i;
					if (r->rport_cnt == 1 && r->rport == 0)
						remotePortCopy = 0;

					r->alink[i] = LibAliasRedirectPort(ptr->lib,
								r->laddr,
								htons(r->lport + i),
								r->raddr,
								htons(remotePortCopy),
								r->paddr,
								htons(r->pport + i),
								r->proto);

					if (r->alink[i] == NULL) {
						r->alink[0] = NULL;
						break;
					}
				}
				break;
			case REDIR_PROTO:
				r->alink[0] = LibAliasRedirectProto(ptr->lib ,r->laddr,
							r->raddr, r->paddr, r->proto);
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
				s = kmalloc(SOF_REDIR, M_IPFW_NAT, M_WAITOK | M_ZERO);
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

static int
ipfw_nat_get_cfg(struct sockopt *sopt)
{
	uint8_t *data;
	struct cfg_nat *n;
	struct cfg_redir *r;
	struct cfg_spool *s;
	int nat_cnt, off, nat_cfg_size;
	size_t size;

	nat_cnt = 0;
	nat_cfg_size = 0;
	off = sizeof(nat_cnt);

	size = sopt->sopt_valsize;

	data = sopt->sopt_val;
	lockmgr(&nat_lock, LK_SHARED);
	/* count the size of nat cfg */
	LIST_FOREACH(n, &((*ipfw_nat_ctx).nat), _next) {
		nat_cfg_size += SOF_NAT;
	}

	LIST_FOREACH(n, &((*ipfw_nat_ctx).nat), _next) {
		nat_cnt++;
		if (off + SOF_NAT < size) {
			bcopy(n, &data[off], SOF_NAT);
			off += SOF_NAT;
			LIST_FOREACH(r, &n->redir_chain, _next) {
				if (off + SOF_REDIR < size) {
					bcopy(r, &data[off], SOF_REDIR);
					off += SOF_REDIR;
					LIST_FOREACH(s, &r->spool_chain, _next) {
						if (off + SOF_SPOOL < size) {
							bcopy(s, &data[off],SOF_SPOOL);
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
	lockmgr(&nat_lock, LK_RELEASE);
	return 0;
nospace:
	lockmgr(&nat_lock, LK_RELEASE);
	bzero(sopt->sopt_val, sopt->sopt_valsize);
	sopt->sopt_valsize = nat_cfg_size;
	return 0;
}

static int
ipfw_nat_get_log(struct sockopt *sopt)
{
	struct cfg_nat *ptr;
	int i, size, cnt, sof;
	uint8_t *data;

	data = NULL;
	sof = LIBALIAS_BUF_SIZE;
	cnt = 0;

	size = i = 0;
	lockmgr(&nat_lock, LK_SHARED);
	LIST_FOREACH(ptr, &((*ipfw_nat_ctx).nat), _next) {
		if (ptr->lib->logDesc == NULL)
			continue;
		cnt++;
		size = cnt * (sof + sizeof(int));
		data = krealloc(data, size, M_IPFW_NAT, M_NOWAIT | M_ZERO);
		if (data == NULL) {
			return ENOSPC;
		}
		bcopy(&ptr->id, &data[i], sizeof(int));
		i += sizeof(int);
		bcopy(ptr->lib->logDesc, &data[i], sof);
		i += sof;
	}
	lockmgr(&nat_lock, LK_RELEASE);
	sooptcopyout(sopt, data, size);
	kfree(data, M_IPFW_NAT);
	return 0;
}

int
ipfw_nat(struct ip_fw_args *args, struct cfg_nat *t, struct mbuf *m)
{
	struct mbuf *mcl;
	struct ip *ip;
	int ldt, retval;
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
			mcl->m_pkthdr.csum_flags &
			CSUM_DELAY_DATA)
		ldt = 1;

	c = mtod(mcl, char *);
	if (args->oif == NULL)
		retval = LibAliasIn(t->lib, c,
				mcl->m_len + M_TRAILINGSPACE(mcl));
	else
		retval = LibAliasOut(t->lib, c,
				mcl->m_len + M_TRAILINGSPACE(mcl));
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
		if ((mcl->m_pkthdr.csum_flags &
					CSUM_DELAY_DATA) == 0) {
			in_delayed_cksum(mcl);
			mcl->m_pkthdr.csum_flags &=
				~CSUM_DELAY_DATA;
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

int ipfw_nat_cfg(struct sockopt *sopt)
{
	struct cfg_nat *ptr, *ser_n;
	char *buf;

	buf = kmalloc(sopt->sopt_valsize, M_IPFW_NAT, M_WAITOK | M_ZERO);
	sooptcopyin(sopt, buf, sopt->sopt_valsize, sizeof(struct cfg_nat));
	ser_n = (struct cfg_nat *)(sopt->sopt_val);

	/*
	 * Find/create nat rule.
	 */
	lockmgr(&nat_lock, LK_EXCLUSIVE);
	LOOKUP_NAT((*ipfw_nat_ctx), ser_n->id, ptr);

	if (ptr == NULL) {
		/* New rule: allocate and init new instance. */
		ptr = kmalloc(sizeof(struct cfg_nat), M_IPFW_NAT, M_WAITOK | M_ZERO);

		if (ptr == NULL) {
			kfree(buf, M_IPFW_NAT);
			return ENOSPC;
		}

		ptr->lib = LibAliasInit(NULL);
		if (ptr->lib == NULL) {
			kfree(ptr, M_IPFW_NAT);
			kfree(buf, M_IPFW_NAT);
			return EINVAL;
		}

		LIST_INIT(&ptr->redir_chain);
	} else {
		/* XXX TODO Entry already exists */
		goto done;
	}

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
	add_redir_spool_cfg(&buf[(sizeof(struct cfg_nat))], ptr);
	HOOK_NAT(&(ipfw_nat_ctx->nat), ptr);
done:
	lockmgr(&nat_lock, LK_RELEASE);
	kfree(buf, M_IPFW_NAT);
	return 0;
}

int
ipfw_nat_del(struct sockopt *sopt)
{
	struct cfg_nat *n;
	int *i;

	i = sopt->sopt_val;
	lockmgr(&nat_lock, LK_EXCLUSIVE);
	LOOKUP_NAT((*ipfw_nat_ctx), *i, n);
	if (n == NULL) {
		return EINVAL;
	}
	UNHOOK_NAT(n);
	del_redir_spool_cfg(n, &n->redir_chain);
	LibAliasUninit(n->lib);
	kfree(n, M_IPFW_NAT);
	lockmgr(&nat_lock, LK_RELEASE);
	return 0;
}

int
ipfw_nat_flush(struct sockopt *sopt)
{
	struct cfg_nat *ptr, *ptr_temp;

	lockmgr(&nat_lock, LK_EXCLUSIVE);
	LIST_FOREACH_MUTABLE(ptr, &(ipfw_nat_ctx->nat), _next, ptr_temp) {
		LIST_REMOVE(ptr, _next);
		del_redir_spool_cfg(ptr, &ptr->redir_chain);
		LibAliasUninit(ptr->lib);
		kfree(ptr, M_IPFW_NAT);
	}
	lockmgr(&nat_lock, LK_RELEASE);
	return 0;
}

static
int ipfw_nat_init(void)
{
	register_ipfw_module(MODULE_NAT_ID, MODULE_NAT_NAME);
	register_ipfw_filter_funcs(MODULE_NAT_ID, O_NAT_NAT, (filter_func)check_nat);
	ipfw_nat_cfg_ptr = ipfw_nat_cfg;
	ipfw_nat_del_ptr = ipfw_nat_del;
	ipfw_nat_flush_ptr = ipfw_nat_flush;
	ipfw_nat_get_cfg_ptr = ipfw_nat_get_cfg;
	ipfw_nat_get_log_ptr = ipfw_nat_get_log;
	return 0;
}

static int
ipfw_nat_stop(void)
{
	struct cfg_nat *ptr, *ptr_temp;

	LIST_FOREACH_MUTABLE(ptr, &(ipfw_nat_ctx->nat), _next, ptr_temp) {
		LIST_REMOVE(ptr, _next);
		del_redir_spool_cfg(ptr, &ptr->redir_chain);
		LibAliasUninit(ptr->lib);
		kfree(ptr, M_IPFW_NAT);
	}

	ipfw_nat_cfg_ptr = NULL;
	ipfw_nat_del_ptr = NULL;
	ipfw_nat_flush_ptr = NULL;
	ipfw_nat_get_cfg_ptr = NULL;
	ipfw_nat_get_log_ptr = NULL;

	return unregister_ipfw_module(MODULE_NAT_ID);
}

static int ipfw_nat_modevent(module_t mod, int type, void *data)
{
	switch (type) {
		case MOD_LOAD:
			return ipfw_nat_init();
		case MOD_UNLOAD:
			return ipfw_nat_stop();
		default:
			break;
	}
	return 0;
}

static moduledata_t ipfw_nat_mod = {
	"ipfw2_nat",
	ipfw_nat_modevent,
	NULL
};

DECLARE_MODULE(ipfw2_nat, ipfw_nat_mod, SI_SUB_PROTO_IFATTACHDOMAIN, SI_ORDER_ANY);
MODULE_DEPEND(ipfw2_nat, libalias, 1, 1, 1);
MODULE_DEPEND(ipfw2_nat, ipfw2_basic, 1, 1, 1);
MODULE_VERSION(ipfw2_nat, 1);
