/*
 * Copyright (c) 2002-2009 Luigi Rizzo, Universita` di Pisa
 *
 * Copyright (c) 2015 - 2018 The DragonFly Project.  All rights reserved.
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

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/ucred.h>
#include <sys/lock.h>
#include <net/ethernet.h>	/* for ETHERTYPE_IP */
#include <net/if.h>
#include <net/if_var.h>
#include <net/ifq_var.h>
#include <net/if_clone.h>
#include <net/if_types.h>	/* for IFT_PFLOG */
#include <net/bpf.h>		/* for BPF */

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip_var.h>
#include <netinet/tcp_var.h>
#include <netinet/udp.h>

#include <net/ipfw3/ip_fw.h>
#include <net/ipfw3_basic/ip_fw3_log.h>

extern int sysctl_var_fw3_verbose;
extern struct if_clone *if_clone_lookup(const char *, int *);

static const char ipfw3_log_ifname[] = "ipfw";
static int log_if_count;
struct ifnet *log_if_table[LOG_IF_MAX];
struct lock log_if_lock;


u_char fake_eh[14] = {0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
			0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x08, 0x00};

static const u_char ipfwbroadcastaddr[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

#define	LOGIF_LOCK_INIT(x) lockinit(&log_if_lock, "fw3log_lk", 0, LK_CANRECURSE)
#define	LOGIF_LOCK_DESTROY(x) lockuninit(&log_if_lock)
#define	LOGIF_RLOCK(x) lockmgr(&log_if_lock, LK_SHARED)
#define	LOGIF_RUNLOCK(x) lockmgr(&log_if_lock, LK_RELEASE)
#define	LOGIF_WLOCK(x) lockmgr(&log_if_lock, LK_EXCLUSIVE)
#define	LOGIF_WUNLOCK(x) lockmgr(&log_if_lock, LK_RELEASE)


/* we use this dummy function for all ifnet callbacks */
static int
ip_fw3_log_dummy(struct ifnet *ifp, u_long cmd, caddr_t addr, struct ucred *uc)
{
	return EINVAL;
}

static int
ip_fw3_log_output(struct ifnet *ifp, struct mbuf *m,
		struct sockaddr *dst, struct rtentry *rtent)
{
	if (m != NULL) {
		m_freem(m);
	}
	return EINVAL;
}

static void
ip_fw3_log_start(struct ifnet* ifp, struct ifaltq_subque *subque)
{
}

/*
 * bpf_mtap into the ipfw interface.
 * eh == NULL when mbuf is a packet, then use the fake_eh
 * the ip_len need to be twisted before and after bpf copy.
 */
void
ip_fw3_log(struct mbuf *m, struct ether_header *eh, uint16_t id)
{
	struct ifnet *the_if = NULL;

	if (sysctl_var_fw3_verbose) {
#ifndef WITHOUT_BPF
		LOGIF_RLOCK();
		the_if = log_if_table[id];
		if (the_if == NULL || the_if->if_bpf == NULL) {
			LOGIF_RUNLOCK();
			return;
		}
		if (eh != NULL) {
			bpf_gettoken();
			bpf_mtap_hdr(the_if->if_bpf, (caddr_t)eh,
					ETHER_HDR_LEN, m, 0);
			bpf_reltoken();

		} else {
			struct ip *ip;
			ip = mtod(m, struct ip *);
			/* twist the ip_len for the bpf copy */
			ip->ip_len =htons(ip->ip_len);

			bpf_gettoken();
			bpf_mtap_hdr(the_if->if_bpf, (caddr_t)fake_eh,
					ETHER_HDR_LEN, m, 0);
			bpf_reltoken();
			ip->ip_len =ntohs(ip->ip_len);

		}
		LOGIF_RUNLOCK();
#endif	/* !WITHOUT_BPF */
	}
}

static int
ip_fw3_log_clone_create(struct if_clone *ifc, int unit,
			caddr_t params __unused, caddr_t data __unused)
{
	struct ifnet *ifp;

	if (unit < 0 || unit >= LOG_IF_MAX) {
		return EINVAL;
	}
	if (log_if_table[unit] != NULL) {
		return EINVAL;
	}

	ifp = if_alloc(IFT_PFLOG);
	if_initname(ifp, ipfw3_log_ifname, unit);
	ifq_set_maxlen(&ifp->if_snd, ifqmaxlen);
	ifq_set_ready(&ifp->if_snd);

	ifp->if_mtu = 65536;
	ifp->if_flags = IFF_UP | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init = (void *)ip_fw3_log_dummy;
	ifp->if_ioctl = ip_fw3_log_dummy;
	ifp->if_start = ip_fw3_log_start;
	ifp->if_output = ip_fw3_log_output;
	ifp->if_addrlen = 6;
	ifp->if_hdrlen = 14;
	ifp->if_broadcastaddr = ipfwbroadcastaddr;
	ifp->if_baudrate = IF_Mbps(10);

	LOGIF_WLOCK();
	log_if_table[unit] = ifp;
	log_if_count++;
	if_attach(ifp, NULL);
	bpfattach(ifp, DLT_EN10MB, ETHER_HDR_LEN);
	LOGIF_WUNLOCK();

	return (0);
}

static int
ip_fw3_log_clone_destroy(struct ifnet *ifp)
{
	int unit;

	if (ifp == NULL)
		return (0);

	unit = ifp->if_dunit;
	if (unit < 0 || unit >= LOG_IF_MAX) {
		return EINVAL;
	}
	if (log_if_table[unit] == NULL) {
		return EINVAL;
	}
	LOGIF_WLOCK();
	log_if_table[unit] = NULL;
	bpfdetach(ifp);
	if_detach(ifp);
	if_free(ifp);
	log_if_count--;
	LOGIF_WUNLOCK();

	return (0);
}

static eventhandler_tag ip_fw3_log_ifdetach_cookie;
static struct if_clone ipfw3_log_cloner = IF_CLONE_INITIALIZER(ipfw3_log_ifname,
		ip_fw3_log_clone_create, ip_fw3_log_clone_destroy, 0, 9);


void ip_fw3_log_modevent(int type){
	struct ifnet *tmpif;
	int i;

	switch (type) {
	case MOD_LOAD:
		LOGIF_LOCK_INIT();
		log_if_count = 0;
		if_clone_attach(&ipfw3_log_cloner);
		ip_fw3_log_ifdetach_cookie =
			EVENTHANDLER_REGISTER(ifnet_detach_event,
				ip_fw3_log_clone_destroy, &ipfw3_log_cloner,
				EVENTHANDLER_PRI_ANY);
		break;
	case MOD_UNLOAD:
		EVENTHANDLER_DEREGISTER(ifnet_detach_event,
					ip_fw3_log_ifdetach_cookie);
		if_clone_detach(&ipfw3_log_cloner);
		for(i = 0; log_if_count > 0 && i < LOG_IF_MAX; i++){
			tmpif = log_if_table[i];
			if (tmpif != NULL) {
				ip_fw3_log_clone_destroy(tmpif);
			}
		}
		LOGIF_LOCK_DESTROY();
		break;

	default:
		break;
	}
}

/* end of file */
