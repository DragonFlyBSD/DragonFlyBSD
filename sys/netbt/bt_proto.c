/* $OpenBSD: bt_proto.c,v 1.4 2007/06/24 20:55:27 uwe Exp $ */

/*
 * Copyright (c) 2004 Alexander Yurchenko <grange@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/queue.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/sysctl.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <net/if.h>

#include <netbt/bluetooth.h>
#include <netbt/hci.h>
#include <netbt/l2cap.h>
#include <netbt/rfcomm.h>
#include <netbt/sco.h>

MALLOC_DEFINE(M_BLUETOOTH, "Bluetooth", "Bluetooth system memory");

extern struct pr_usrreqs hci_usrreqs;

static int
netbt_modevent(module_t mod, int type, void *data)
{
	switch (type) {
	case MOD_LOAD:
		break;
	case MOD_UNLOAD:
		return EBUSY;
		break;
	default:
		break;
	}
	return 0;
}

static moduledata_t netbt_mod = {
	"netbt",
	netbt_modevent,
	NULL
};

DECLARE_MODULE(netbt, netbt_mod, SI_SUB_EXEC, SI_ORDER_ANY);
MODULE_VERSION(netbt, 1);

struct domain btdomain;

struct protosw btsw[] = {
	{ /* raw HCI commands */
		.pr_type = SOCK_RAW,
		.pr_domain = &btdomain,
		.pr_protocol = BTPROTO_HCI,
		.pr_flags = (PR_ADDR | PR_ATOMIC),
		.pr_input = 0,
		.pr_output = 0,
		.pr_ctlinput = 0,
		.pr_ctloutput = hci_ctloutput,
		.pr_ctlport = NULL,
		.pr_init = 0,
		.pr_fasttimo =	0,
		.pr_slowtimo = 0,
		.pr_drain = 0,
		.pr_usrreqs = &hci_usrreqs
	},
	{ /* HCI SCO data (audio) */
		.pr_type = SOCK_SEQPACKET,
		.pr_domain = &btdomain,
		.pr_protocol = BTPROTO_SCO,
		.pr_flags = (PR_CONNREQUIRED | PR_ATOMIC ),
		.pr_input = 0,
		.pr_output = 0,
		.pr_ctlinput = 0,
		.pr_ctloutput = sco_ctloutput,
		.pr_ctlport = NULL,
		.pr_init = 0,
		.pr_fasttimo =	0,
		.pr_slowtimo = 0,
		.pr_drain = 0,
		.pr_usrreqs = &sco_usrreqs

	},
	{ /* L2CAP Connection Oriented */
		.pr_type = SOCK_SEQPACKET,
		.pr_domain = &btdomain,
		.pr_protocol = BTPROTO_L2CAP,
		.pr_flags = (PR_CONNREQUIRED | PR_ATOMIC ),
		.pr_input = 0,
		.pr_output = 0,
		.pr_ctlinput = 0,
		.pr_ctloutput = l2cap_ctloutput,
		.pr_ctlport = NULL,
		.pr_init = 0,
		.pr_fasttimo =	0,
		.pr_slowtimo = 0,
		.pr_drain = 0,
		.pr_usrreqs = &l2cap_usrreqs
	},
	{ /* RFCOMM */
		.pr_type = SOCK_STREAM,
		.pr_domain = &btdomain,
		.pr_protocol = BTPROTO_RFCOMM,
		.pr_flags = (PR_CONNREQUIRED | PR_WANTRCVD),
		.pr_input = 0,
		.pr_output = 0,
		.pr_ctlinput = 0,
		.pr_ctloutput = rfcomm_ctloutput,
		.pr_ctlport = NULL,
		.pr_init = 0,
		.pr_fasttimo =	0,
		.pr_slowtimo = 0,
		.pr_drain = 0,
		.pr_usrreqs = &rfcomm_usrreqs
	},
};

static void
netbt_dispose(struct mbuf* m)
{
	objcache_destroy(l2cap_pdu_pool);
	objcache_destroy(l2cap_req_pool);
	objcache_destroy(rfcomm_credit_pool);
}

static void
netbt_init(void)
{
	l2cap_pdu_pool =
	    objcache_create_simple(M_BLUETOOTH, sizeof(struct l2cap_pdu));
	if (l2cap_pdu_pool == NULL)
		goto fail;
	l2cap_req_pool =
	    objcache_create_simple(M_BLUETOOTH, sizeof(struct l2cap_req));
	if (l2cap_req_pool == NULL)
		goto fail;
	rfcomm_credit_pool =
	    objcache_create_simple(M_BLUETOOTH, sizeof(struct rfcomm_credit));
	if (rfcomm_credit_pool == NULL)
		goto fail;
	return;
fail:
	netbt_dispose(NULL);
	panic("Can't create magazine out of slab");
}

struct domain btdomain = {
	.dom_family = AF_BLUETOOTH,
	.dom_name = "bluetooth",
	.dom_init = netbt_init,
	.dom_externalize = NULL,
	.dom_dispose = netbt_dispose,
	.dom_protosw = btsw,
	.dom_protoswNPROTOSW = &btsw[NELEM(btsw)],
	.dom_next = SLIST_ENTRY_INITIALIZER,
	.dom_rtattach = 0,
	.dom_rtoffset = 32,
	.dom_maxrtkey = sizeof(struct sockaddr_bt),
	.dom_ifattach = 0,
	.dom_ifdetach = 0,
};

DOMAIN_SET(bt);
SYSCTL_NODE(_net, OID_AUTO, bluetooth, CTLFLAG_RD, 0,
    "Bluetooth Protocol Family");

/* HCI sysctls */
SYSCTL_NODE(_net_bluetooth, OID_AUTO, hci, CTLFLAG_RD, 0,
    "Host Controller Interface");
SYSCTL_INT(_net_bluetooth_hci, OID_AUTO, sendspace, CTLFLAG_RW, &hci_sendspace,
    0, "Socket Send Buffer Size");
SYSCTL_INT(_net_bluetooth_hci, OID_AUTO, recvspace, CTLFLAG_RW, &hci_recvspace,
    0, "Socket Receive Buffer Size");
SYSCTL_INT(_net_bluetooth_hci, OID_AUTO, acl_expiry, CTLFLAG_RW,
    &hci_acl_expiry, 0, "ACL Connection Expiry Time");
SYSCTL_INT(_net_bluetooth_hci, OID_AUTO, memo_expiry, CTLFLAG_RW,
    &hci_memo_expiry, 0, "Memo Expiry Time");
SYSCTL_INT(_net_bluetooth_hci, OID_AUTO, eventq_max, CTLFLAG_RW,
    &hci_eventq_max, 0, "Max Event queue length");
SYSCTL_INT(_net_bluetooth_hci, OID_AUTO, aclrxq_max, CTLFLAG_RW,
    &hci_aclrxq_max, 0, "Max ACL rx queue length");
SYSCTL_INT(_net_bluetooth_hci, OID_AUTO, scorxq_max, CTLFLAG_RW,
    &hci_scorxq_max, 0, "Max SCO rx queue length");

/* L2CAP sysctls */
SYSCTL_NODE(_net_bluetooth, OID_AUTO, l2cap, CTLFLAG_RD, 0,
    "Logical Link Control & Adaptation Protocol");
SYSCTL_INT(_net_bluetooth_l2cap, OID_AUTO, sendspace, CTLFLAG_RW,
    &l2cap_sendspace, 0, "Socket Send Buffer Size");
SYSCTL_INT(_net_bluetooth_l2cap, OID_AUTO, recvspace, CTLFLAG_RW,
    &l2cap_recvspace, 0, "Socket Receive Buffer Size");
SYSCTL_INT(_net_bluetooth_l2cap, OID_AUTO, rtx, CTLFLAG_RW,
    &l2cap_response_timeout, 0, "Response Timeout");
SYSCTL_INT(_net_bluetooth_l2cap, OID_AUTO, ertx, CTLFLAG_RW,
    &l2cap_response_extended_timeout, 0, "Extended Response Timeout");

/* RFCOMM sysctls */
SYSCTL_NODE(_net_bluetooth, OID_AUTO, rfcomm, CTLFLAG_RD, 0,
    "Serial Cable Emulation");
SYSCTL_INT(_net_bluetooth_rfcomm, OID_AUTO, sendspace, CTLFLAG_RW,
    &rfcomm_sendspace, 0, "Socket Send Buffer Size");
SYSCTL_INT(_net_bluetooth_rfcomm, OID_AUTO, recvspace, CTLFLAG_RW,
    &rfcomm_recvspace, 0, "Socket Receive Buffer Size");
SYSCTL_INT(_net_bluetooth_rfcomm, OID_AUTO, mtu_default, CTLFLAG_RW,
    &rfcomm_mtu_default, 0, "Default MTU");
SYSCTL_INT(_net_bluetooth_rfcomm, OID_AUTO, ack_timeout, CTLFLAG_RW,
    &rfcomm_ack_timeout, 0, "Acknowledgement Timer");
SYSCTL_INT(_net_bluetooth_rfcomm, OID_AUTO, mcc_timeout, CTLFLAG_RW,
    &rfcomm_mcc_timeout, 0, "Response Timeout for Multiplexer Control Channel");

/* SCO sysctls */
SYSCTL_NODE(_net_bluetooth, OID_AUTO, sco, CTLFLAG_RD, 0, "SCO data");
SYSCTL_INT(_net_bluetooth_sco, OID_AUTO, sendspace, CTLFLAG_RW, &sco_sendspace,
    0, "Socket Send Buffer Size");
SYSCTL_INT(_net_bluetooth_sco, OID_AUTO, recvspace, CTLFLAG_RW, &sco_recvspace,
    0, "Socket Receive Buffer Size");

static void
netisr_netbt_setup(void *dummy __unused)
{
	netisr_register(NETISR_BLUETOOTH, btintr, NULL);
}

SYSINIT(netbt_setup, SI_BOOT2_KLD, SI_ORDER_ANY, netisr_netbt_setup, NULL);
