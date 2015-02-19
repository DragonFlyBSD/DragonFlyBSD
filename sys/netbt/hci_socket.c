/* $OpenBSD: src/sys/netbt/hci_socket.c,v 1.5 2008/02/24 21:34:48 uwe Exp $ */
/* $NetBSD: hci_socket.c,v 1.14 2008/02/10 17:40:54 plunky Exp $ */

/*-
 * Copyright (c) 2005 Iain Hibbert.
 * Copyright (c) 2006 Itronix Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of Itronix Inc. may not be used to endorse
 *    or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ITRONIX INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL ITRONIX INC. BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* load symbolic names */
#ifdef BLUETOOTH_DEBUG
#define PRUREQUESTS
#define PRCOREQUESTS
#endif

#include <sys/param.h>
#include <sys/domain.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/systm.h>
#include <sys/endian.h>
#include <net/if.h>
#include <net/if_var.h>
#include <sys/sysctl.h>

#include <sys/thread2.h>
#include <sys/socketvar2.h>
#include <sys/msgport2.h>

#include <netbt/bluetooth.h>
#include <netbt/hci.h>

/*******************************************************************************
 *
 * HCI SOCK_RAW Sockets - for control of Bluetooth Devices
 *
 */

/*
 * the raw HCI protocol control block
 */
struct hci_pcb {
	struct socket		*hp_socket;	/* socket */
	unsigned int		hp_flags;	/* flags */
	bdaddr_t		hp_laddr;	/* local address */
	bdaddr_t		hp_raddr;	/* remote address */
	struct hci_filter	hp_efilter;	/* user event filter */
	struct hci_filter	hp_pfilter;	/* user packet filter */
	LIST_ENTRY(hci_pcb)	hp_next;	/* next HCI pcb */
};

/* hp_flags */
#define HCI_PRIVILEGED		(1<<0)	/* no security filter for root */
#define HCI_DIRECTION		(1<<1)	/* direction control messages */
#define HCI_PROMISCUOUS		(1<<2)	/* listen to all units */

LIST_HEAD(hci_pcb_list, hci_pcb) hci_pcb = LIST_HEAD_INITIALIZER(hci_pcb);

/* sysctl defaults */
int hci_sendspace = HCI_CMD_PKT_SIZE;
int hci_recvspace = 4096;

extern struct pr_usrreqs hci_usrreqs;

/* Prototypes for usrreqs methods. */
static void hci_sdetach(netmsg_t msg);

/* supported commands opcode table */
static const struct {
	uint16_t	opcode;
	uint8_t		offs;	/* 0 - 63 */
	uint8_t		mask;	/* bit 0 - 7 */
	int16_t		length;	/* -1 if privileged */
} hci_cmds[] = {
	{ HCI_CMD_INQUIRY,
	  0,  0x01, sizeof(hci_inquiry_cp) },
	{ HCI_CMD_INQUIRY_CANCEL,
	  0,  0x02, -1 },
	{ HCI_CMD_PERIODIC_INQUIRY,
	  0,  0x04, -1 },
	{ HCI_CMD_EXIT_PERIODIC_INQUIRY,
	  0,  0x08, -1 },
	{ HCI_CMD_CREATE_CON,
	  0,  0x10, -1 },
	{ HCI_CMD_DISCONNECT,
	  0,  0x20, -1 },
	{ HCI_CMD_ADD_SCO_CON,
	  0,  0x40, -1 },
	{ HCI_CMD_CREATE_CON_CANCEL,
	  0,  0x80, -1 },
	{ HCI_CMD_ACCEPT_CON,
	  1,  0x01, -1 },
	{ HCI_CMD_REJECT_CON,
	  1,  0x02, -1 },
	{ HCI_CMD_LINK_KEY_REP,
	  1,  0x04, -1 },
	{ HCI_CMD_LINK_KEY_NEG_REP,
	  1,  0x08, -1 },
	{ HCI_CMD_PIN_CODE_REP,
	  1,  0x10, -1 },
	{ HCI_CMD_PIN_CODE_NEG_REP,
	  1,  0x20, -1 },
	{ HCI_CMD_CHANGE_CON_PACKET_TYPE,
	  1,  0x40, -1 },
	{ HCI_CMD_AUTH_REQ,
	  1,  0x80, -1 },
	{ HCI_CMD_SET_CON_ENCRYPTION,
	  2,  0x01, -1 },
	{ HCI_CMD_CHANGE_CON_LINK_KEY,
	  2,  0x02, -1 },
	{ HCI_CMD_MASTER_LINK_KEY,
	  2,  0x04, -1 },
	{ HCI_CMD_REMOTE_NAME_REQ,
	  2,  0x08, sizeof(hci_remote_name_req_cp) },
	{ HCI_CMD_REMOTE_NAME_REQ_CANCEL,
	  2,  0x10, -1 },
	{ HCI_CMD_READ_REMOTE_FEATURES,
	  2,  0x20, sizeof(hci_read_remote_features_cp) },
	{ HCI_CMD_READ_REMOTE_EXTENDED_FEATURES,
	  2,  0x40, sizeof(hci_read_remote_extended_features_cp) },
	{ HCI_CMD_READ_REMOTE_VER_INFO,
	  2,  0x80, sizeof(hci_read_remote_ver_info_cp) },
	{ HCI_CMD_READ_CLOCK_OFFSET,
	  3,  0x01, sizeof(hci_read_clock_offset_cp) },
	{ HCI_CMD_READ_LMP_HANDLE,
	  3,  0x02, sizeof(hci_read_lmp_handle_cp) },
	{ HCI_CMD_HOLD_MODE,
	  4,  0x02, -1 },
	{ HCI_CMD_SNIFF_MODE,
	  4,  0x04, -1 },
	{ HCI_CMD_EXIT_SNIFF_MODE,
	  4,  0x08, -1 },
	{ HCI_CMD_PARK_MODE,
	  4,  0x10, -1 },
	{ HCI_CMD_EXIT_PARK_MODE,
	  4,  0x20, -1 },
	{ HCI_CMD_QOS_SETUP,
	  4,  0x40, -1 },
	{ HCI_CMD_ROLE_DISCOVERY,
	  4,  0x80, sizeof(hci_role_discovery_cp) },
	{ HCI_CMD_SWITCH_ROLE,
	  5,  0x01, -1 },
	{ HCI_CMD_READ_LINK_POLICY_SETTINGS,
	  5,  0x02, sizeof(hci_read_link_policy_settings_cp) },
	{ HCI_CMD_WRITE_LINK_POLICY_SETTINGS,
	  5,  0x04, -1 },
	{ HCI_CMD_READ_DEFAULT_LINK_POLICY_SETTINGS,
	  5,  0x08, 0 },
	{ HCI_CMD_WRITE_DEFAULT_LINK_POLICY_SETTINGS,
	  5,  0x10, -1 },
	{ HCI_CMD_FLOW_SPECIFICATION,
	  5,  0x20, -1 },
	{ HCI_CMD_SET_EVENT_MASK,
	  5,  0x40, -1 },
	{ HCI_CMD_RESET,
	  5,  0x80, -1 },
	{ HCI_CMD_SET_EVENT_FILTER,
	  6,  0x01, -1 },
	{ HCI_CMD_FLUSH,
	  6,  0x02, -1 },
	{ HCI_CMD_READ_PIN_TYPE,
	  6,  0x04, 0 },
	{ HCI_CMD_WRITE_PIN_TYPE,
	  6,  0x08, -1 },
	{ HCI_CMD_CREATE_NEW_UNIT_KEY,
	  6,  0x10, -1 },
	{ HCI_CMD_READ_STORED_LINK_KEY,
	  6,  0x20, -1 },
	{ HCI_CMD_WRITE_STORED_LINK_KEY,
	  6,  0x40, -1 },
	{ HCI_CMD_DELETE_STORED_LINK_KEY,
	  6,  0x80, -1 },
	{ HCI_CMD_WRITE_LOCAL_NAME,
	  7,  0x01, -1 },
	{ HCI_CMD_READ_LOCAL_NAME,
	  7,  0x02, 0 },
	{ HCI_CMD_READ_CON_ACCEPT_TIMEOUT,
	  7,  0x04, 0 },
	{ HCI_CMD_WRITE_CON_ACCEPT_TIMEOUT,
	  7,  0x08, -1 },
	{ HCI_CMD_READ_PAGE_TIMEOUT,
	  7,  0x10, 0 },
	{ HCI_CMD_WRITE_PAGE_TIMEOUT,
	  7,  0x20, -1 },
	{ HCI_CMD_READ_SCAN_ENABLE,
	  7,  0x40, 0 },
	{ HCI_CMD_WRITE_SCAN_ENABLE,
	  7,  0x80, -1 },
	{ HCI_CMD_READ_PAGE_SCAN_ACTIVITY,
	  8,  0x01, 0 },
	{ HCI_CMD_WRITE_PAGE_SCAN_ACTIVITY,
	  8,  0x02, -1 },
	{ HCI_CMD_READ_INQUIRY_SCAN_ACTIVITY,
	  8,  0x04, 0 },
	{ HCI_CMD_WRITE_INQUIRY_SCAN_ACTIVITY,
	  8,  0x08, -1 },
	{ HCI_CMD_READ_AUTH_ENABLE,
	  8,  0x10, 0 },
	{ HCI_CMD_WRITE_AUTH_ENABLE,
	  8,  0x20, -1 },
	{ HCI_CMD_READ_ENCRYPTION_MODE,
	  8,  0x40, 0 },
	{ HCI_CMD_WRITE_ENCRYPTION_MODE,
	  8,  0x80, -1 },
	{ HCI_CMD_READ_UNIT_CLASS,
	  9,  0x01, 0 },
	{ HCI_CMD_WRITE_UNIT_CLASS,
	  9,  0x02, -1 },
	{ HCI_CMD_READ_VOICE_SETTING,
	  9,  0x04, 0 },
	{ HCI_CMD_WRITE_VOICE_SETTING,
	  9,  0x08, -1 },
	{ HCI_CMD_READ_AUTO_FLUSH_TIMEOUT,
	  9,  0x10, sizeof(hci_read_auto_flush_timeout_cp) },
	{ HCI_CMD_WRITE_AUTO_FLUSH_TIMEOUT,
	  9,  0x20, -1 },
	{ HCI_CMD_READ_NUM_BROADCAST_RETRANS,
	  9,  0x40, 0 },
	{ HCI_CMD_WRITE_NUM_BROADCAST_RETRANS,
	  9,  0x80, -1 },
	{ HCI_CMD_READ_HOLD_MODE_ACTIVITY,
	  10, 0x01, 0 },
	{ HCI_CMD_WRITE_HOLD_MODE_ACTIVITY,
	  10, 0x02, -1 },
	{ HCI_CMD_READ_XMIT_LEVEL,
	  10, 0x04, sizeof(hci_read_xmit_level_cp) },
	{ HCI_CMD_READ_SCO_FLOW_CONTROL,
	  10, 0x08, 0 },
	{ HCI_CMD_WRITE_SCO_FLOW_CONTROL,
	  10, 0x10, -1 },
	{ HCI_CMD_HC2H_FLOW_CONTROL,
	  10, 0x20, -1 },
	{ HCI_CMD_HOST_BUFFER_SIZE,
	  10, 0x40, -1 },
	{ HCI_CMD_HOST_NUM_COMPL_PKTS,
	  10, 0x80, -1 },
	{ HCI_CMD_READ_LINK_SUPERVISION_TIMEOUT,
	  11, 0x01, sizeof(hci_read_link_supervision_timeout_cp) },
	{ HCI_CMD_WRITE_LINK_SUPERVISION_TIMEOUT,
	  11, 0x02, -1 },
	{ HCI_CMD_READ_NUM_SUPPORTED_IAC,
	  11, 0x04, 0 },
	{ HCI_CMD_READ_IAC_LAP,
	  11, 0x08, 0 },
	{ HCI_CMD_WRITE_IAC_LAP,
	  11, 0x10, -1 },
	{ HCI_CMD_READ_PAGE_SCAN_PERIOD,
	  11, 0x20, 0 },
	{ HCI_CMD_WRITE_PAGE_SCAN_PERIOD,
	  11, 0x40, -1 },
	{ HCI_CMD_READ_PAGE_SCAN,
	  11, 0x80, 0 },
	{ HCI_CMD_WRITE_PAGE_SCAN,
	  12, 0x01, -1 },
	{ HCI_CMD_SET_AFH_CLASSIFICATION,
	  12, 0x02, -1 },
	{ HCI_CMD_READ_INQUIRY_SCAN_TYPE,
	  12, 0x10, 0 },
	{ HCI_CMD_WRITE_INQUIRY_SCAN_TYPE,
	  12, 0x20, -1 },
	{ HCI_CMD_READ_INQUIRY_MODE,
	  12, 0x40, 0 },
	{ HCI_CMD_WRITE_INQUIRY_MODE,
	  12, 0x80, -1 },
	{ HCI_CMD_READ_PAGE_SCAN_TYPE,
	  13, 0x01, 0 },
	{ HCI_CMD_WRITE_PAGE_SCAN_TYPE,
	  13, 0x02, -1 },
	{ HCI_CMD_READ_AFH_ASSESSMENT,
	  13, 0x04, 0 },
	{ HCI_CMD_WRITE_AFH_ASSESSMENT,
	  13, 0x08, -1 },
	{ HCI_CMD_READ_LOCAL_VER,
	  14, 0x08, 0 },
	{ HCI_CMD_READ_LOCAL_COMMANDS,
	  14, 0x10, 0 },
	{ HCI_CMD_READ_LOCAL_FEATURES,
	  14, 0x20, 0 },
	{ HCI_CMD_READ_LOCAL_EXTENDED_FEATURES,
	  14, 0x40, sizeof(hci_read_local_extended_features_cp) },
	{ HCI_CMD_READ_BUFFER_SIZE,
	  14, 0x80, 0 },
	{ HCI_CMD_READ_COUNTRY_CODE,
	  15, 0x01, 0 },
	{ HCI_CMD_READ_BDADDR,
	  15, 0x02, 0 },
	{ HCI_CMD_READ_FAILED_CONTACT_CNTR,
	  15, 0x04, sizeof(hci_read_failed_contact_cntr_cp) },
	{ HCI_CMD_RESET_FAILED_CONTACT_CNTR,
	  15, 0x08, -1 },
	{ HCI_CMD_READ_LINK_QUALITY,
	  15, 0x10, sizeof(hci_read_link_quality_cp) },
	{ HCI_CMD_READ_RSSI,
	  15, 0x20, sizeof(hci_read_rssi_cp) },
	{ HCI_CMD_READ_AFH_CHANNEL_MAP,
	  15, 0x40, sizeof(hci_read_afh_channel_map_cp) },
	{ HCI_CMD_READ_CLOCK,
	  15, 0x80, sizeof(hci_read_clock_cp) },
	{ HCI_CMD_READ_LOOPBACK_MODE,
	  16, 0x01, 0 },
	{ HCI_CMD_WRITE_LOOPBACK_MODE,
	  16, 0x02, -1 },
	{ HCI_CMD_ENABLE_UNIT_UNDER_TEST,
	  16, 0x04, -1 },
	{ HCI_CMD_SETUP_SCO_CON,
	  16, 0x08, -1 },
	{ HCI_CMD_ACCEPT_SCO_CON_REQ,
	  16, 0x10, -1 },
	{ HCI_CMD_REJECT_SCO_CON_REQ,
	  16, 0x20, -1 },
	{ HCI_CMD_READ_EXTENDED_INQUIRY_RSP,
	  17, 0x01, 0 },
	{ HCI_CMD_WRITE_EXTENDED_INQUIRY_RSP,
	  17, 0x02, -1 },
	{ HCI_CMD_REFRESH_ENCRYPTION_KEY,
	  17, 0x04, -1 },
	{ HCI_CMD_SNIFF_SUBRATING,
	  17, 0x10, -1 },
	{ HCI_CMD_READ_SIMPLE_PAIRING_MODE,
	  17, 0x20, 0 },
	{ HCI_CMD_WRITE_SIMPLE_PAIRING_MODE,
	  17, 0x40, -1 },
	{ HCI_CMD_READ_LOCAL_OOB_DATA,
	  17, 0x80, -1 },
	{ HCI_CMD_READ_INQUIRY_RSP_XMIT_POWER,
	  18, 0x01, 0 },
	{ HCI_CMD_WRITE_INQUIRY_RSP_XMIT_POWER,
	  18, 0x02, -1 },
	{ HCI_CMD_READ_DEFAULT_ERRDATA_REPORTING,
	  18, 0x04, 0 },
	{ HCI_CMD_WRITE_DEFAULT_ERRDATA_REPORTING,
	  18, 0x08, -1 },
	{ HCI_CMD_IO_CAPABILITY_REP,
	  18, 0x80, -1 },
	{ HCI_CMD_USER_CONFIRM_REP,
	  19, 0x01, -1 },
	{ HCI_CMD_USER_CONFIRM_NEG_REP,
	  19, 0x02, -1 },
	{ HCI_CMD_USER_PASSKEY_REP,
	  19, 0x04, -1 },
	{ HCI_CMD_USER_PASSKEY_NEG_REP,
	  19, 0x08, -1 },
	{ HCI_CMD_OOB_DATA_REP,
	  19, 0x10, -1 },
	{ HCI_CMD_WRITE_SIMPLE_PAIRING_DEBUG_MODE,
	  19, 0x20, -1 },
	{ HCI_CMD_ENHANCED_FLUSH,
	  19, 0x40, -1 },
	{ HCI_CMD_OOB_DATA_NEG_REP,
	  19, 0x80, -1 },
	{ HCI_CMD_SEND_KEYPRESS_NOTIFICATION,
	  20, 0x40, -1 },
	{ HCI_CMD_IO_CAPABILITY_NEG_REP,
	  20, 0x80, -1 },
};

/*
 * Security filter routines for unprivileged users.
 *	Allow all but a few critical events, and only permit read commands.
 *	If a unit is given, verify the command is supported.
 */

static int
hci_security_check_opcode(struct hci_unit *unit, uint16_t opcode)
{
	int i;

	for (i = 0 ; i < NELEM(hci_cmds); i++) {
		if (opcode != hci_cmds[i].opcode)
			continue;

		if (unit == NULL
		    || (unit->hci_cmds[hci_cmds[i].offs] & hci_cmds[i].mask))
			return hci_cmds[i].length;

		break;
	}

	return -1;
}

static int
hci_security_check_event(uint8_t event)
{

	switch (event) {
	case HCI_EVENT_RETURN_LINK_KEYS:
	case HCI_EVENT_LINK_KEY_NOTIFICATION:
	case HCI_EVENT_USER_CONFIRM_REQ:
	case HCI_EVENT_USER_PASSKEY_NOTIFICATION:
	case HCI_EVENT_VENDOR:
		return -1;	/* disallowed */
	}

	return 0;	/* ok */
}

/*
 * When command packet reaches the device, we can drop
 * it from the socket buffer (called from hci_output_acl)
 */
void
hci_drop(void *arg)
{
	struct socket *so = arg;

	sbdroprecord(&so->so_snd.sb);
	sowwakeup(so);
}

/*
 * HCI socket is going away and has some pending packets. We let them
 * go by design, but remove the context pointer as it will be invalid
 * and we no longer need to be notified.
 */
static void
hci_cmdwait_flush(struct socket *so)
{
	struct hci_unit *unit;
	struct socket *ctx;
	struct mbuf *m;

	DPRINTF("flushing %p\n", so);

	TAILQ_FOREACH(unit, &hci_unit_list, hci_next) {
		IF_POLL(&unit->hci_cmdwait, m);
		while (m != NULL) {
			ctx = M_GETCTX(m, struct socket *);
			if (ctx == so)
				M_SETCTX(m, NULL);

			m = m->m_nextpkt;
		}
	}
}

/*
 * HCI send packet
 *     This came from userland, so check it out.
 */
static int
hci_send(struct hci_pcb *pcb, struct mbuf *m, bdaddr_t *addr)
{
	struct hci_unit *unit;
	struct mbuf *m0;
	hci_cmd_hdr_t hdr;
	int err;

	KKASSERT(m != NULL);
	KKASSERT(addr != NULL);

	/* wants at least a header to start with */
	if (m->m_pkthdr.len < sizeof(hdr)) {
		err = EMSGSIZE;
		goto bad;
	}
	m_copydata(m, 0, sizeof(hdr), (caddr_t)&hdr);
	hdr.opcode = letoh16(hdr.opcode);

	/* only allows CMD packets to be sent */
	if (hdr.type != HCI_CMD_PKT) {
		err = EINVAL;
		goto bad;
	}

	/* validates packet length */
	if (m->m_pkthdr.len != sizeof(hdr) + hdr.length) {
		err = EMSGSIZE;
		goto bad;
	}

	/* finds destination */
	unit = hci_unit_lookup(addr);
	if (unit == NULL) {
		err = ENETDOWN;
		goto bad;
	}

	/* security checks for unprivileged users */
	if ((pcb->hp_flags & HCI_PRIVILEGED) == 0
	    && hci_security_check_opcode(unit, hdr.opcode) != hdr.length) {
		err = EPERM;
		goto bad;
	}

	/* makes a copy for precious to keep */
	m0 = m_copym(m, 0, M_COPYALL, M_NOWAIT);
	if (m0 == NULL) {
		err = ENOMEM;
		goto bad;
	}
	sbappendrecord(&pcb->hp_socket->so_snd.sb, m0);
	M_SETCTX(m, pcb->hp_socket);	/* enable drop callback */

	DPRINTFN(2, "(%s) opcode (%03x|%04x)\n",
		device_get_nameunit(unit->hci_dev),
		HCI_OGF(hdr.opcode), HCI_OCF(hdr.opcode));

	/* Sendss it */
	if (unit->hci_num_cmd_pkts == 0)
		IF_ENQUEUE(&unit->hci_cmdwait, m);
	else
		hci_output_cmd(unit, m);

	return 0;

bad:
	DPRINTF("packet (%d bytes) not sent (error %d)\n",
	    m->m_pkthdr.len, err);
	if (m) m_freem(m);
	return err;
}

/*
 * Implementation of usrreqs.
 *
 * NOTE: (so) is referenced from soabort*() and netmsg_pru_abort()
 *	 will sofree() it when we return.
 */
static void
hci_sabort(netmsg_t msg)
{
	/* struct hci_pcb *pcb = (struct hci_pcb *)so->so_pcb;	*/

	soisdisconnected(msg->abort.base.nm_so);
	hci_sdetach(msg);
	/* msg now invalid */
}

static void
hci_sdetach(netmsg_t msg)
{
	struct socket *so = msg->detach.base.nm_so;
	struct hci_pcb *pcb = (struct hci_pcb *)so->so_pcb;	
	int error;
	
	if (pcb == NULL) {
		error = EINVAL;
	} else {
		if (so->so_snd.ssb_mb != NULL)
			hci_cmdwait_flush(so);

		so->so_pcb = NULL;
		sofree(so);		/* remove pcb ref */

		LIST_REMOVE(pcb, hp_next);
		kfree(pcb, M_PCB);
		error = 0;
	}
	lwkt_replymsg(&msg->detach.base.lmsg, error);
}

static void
hci_sdisconnect(netmsg_t msg)
{
	struct socket *so = msg->disconnect.base.nm_so;
	struct hci_pcb *pcb = (struct hci_pcb *)so->so_pcb;	
	int error;

	if (pcb) {
		bdaddr_copy(&pcb->hp_raddr, BDADDR_ANY);
		/*
		 * XXX We cannot call soisdisconnected() here, as it sets
		 * SS_CANTRCVMORE and SS_CANTSENDMORE. The problem is that
		 * soisconnected() does not clear these and if you try to
		 * reconnect this socket (which is permitted) you get a
		 * broken pipe when you try to write any data.
		 */
		soclrstate(so, SS_ISCONNECTED);
		error = 0;
	} else {
		error = EINVAL;
	}
	lwkt_replymsg(&msg->disconnect.base.lmsg, error);
}

static void
hci_scontrol(netmsg_t msg)
{
	int error;

	error = hci_ioctl(msg->control.nm_cmd,
			  (void *)msg->control.nm_data,
			  NULL);
	lwkt_replymsg(&msg->control.base.lmsg, error);
}

static void
hci_sattach(netmsg_t msg)
{
	struct socket *so = msg->attach.base.nm_so;
	struct hci_pcb *pcb = (struct hci_pcb *)so->so_pcb;
	int error;

	if (pcb) {
		error = EINVAL;
		goto out;
	}

	error = soreserve(so, hci_sendspace, hci_recvspace,NULL);
	if (error)
		goto out;

	pcb = kmalloc(sizeof *pcb, M_PCB, M_NOWAIT | M_ZERO);
	if (pcb == NULL) {
		error = ENOMEM;
		goto out;
	}

	soreference(so);
	so->so_pcb = pcb;
	pcb->hp_socket = so;

	if (curproc == NULL || priv_check(curthread, PRIV_ROOT) == 0)
		pcb->hp_flags |= HCI_PRIVILEGED;

	/*
	 * Set default user filter. By default, socket only passes
	 * Command_Complete and Command_Status Events.
	 */
	hci_filter_set(HCI_EVENT_COMMAND_COMPL, &pcb->hp_efilter);
	hci_filter_set(HCI_EVENT_COMMAND_STATUS, &pcb->hp_efilter);
	hci_filter_set(HCI_EVENT_PKT, &pcb->hp_pfilter);

	crit_enter();
	LIST_INSERT_HEAD(&hci_pcb, pcb, hp_next);
	crit_exit();
	error = 0;
out:
	lwkt_replymsg(&msg->attach.base.lmsg, error);
}

static void
hci_sbind(netmsg_t msg)
{
	struct socket *so = msg->bind.base.nm_so;
	struct sockaddr *nam = msg->bind.nm_nam;
	struct hci_pcb *pcb = (struct hci_pcb *)so->so_pcb;
	struct sockaddr_bt *sa;
	int error;

	KKASSERT(nam != NULL);
	sa = (struct sockaddr_bt *)nam;

	if (sa->bt_len != sizeof(struct sockaddr_bt)) {
		error = EINVAL;
		goto out;
	}

	if (sa->bt_family != AF_BLUETOOTH) {
		error = EAFNOSUPPORT;
		goto out;
	}

	bdaddr_copy(&pcb->hp_laddr, &sa->bt_bdaddr);

	if (bdaddr_any(&sa->bt_bdaddr))
		pcb->hp_flags |= HCI_PROMISCUOUS;
	else
		pcb->hp_flags &= ~HCI_PROMISCUOUS;
	error = 0;
out:
	lwkt_replymsg(&msg->bind.base.lmsg, error);
}

static void
hci_sconnect(netmsg_t msg)
{
	struct socket *so = msg->connect.base.nm_so;
	struct sockaddr *nam = msg->connect.nm_nam;
	struct hci_pcb *pcb = (struct hci_pcb *)so->so_pcb;
	struct sockaddr_bt *sa;
	int error;

	KKASSERT(nam != NULL);
	sa = (struct sockaddr_bt *)nam;

	if (sa->bt_len != sizeof(struct sockaddr_bt)) {
		error = EINVAL;
		goto out;
	}

	if (sa->bt_family != AF_BLUETOOTH) {
		error =  EAFNOSUPPORT;
		goto out;
	}

	if (hci_unit_lookup(&sa->bt_bdaddr) == NULL) {
		error = EADDRNOTAVAIL;
		goto out;
	}
	bdaddr_copy(&pcb->hp_raddr, &sa->bt_bdaddr);
	soisconnected(so);
	error = 0;
out:
	lwkt_replymsg(&msg->connect.base.lmsg, error);
}

static void
hci_speeraddr(netmsg_t msg)
{
	struct socket *so = msg->peeraddr.base.nm_so;
	struct sockaddr **nam = msg->peeraddr.nm_nam;
	struct hci_pcb *pcb = (struct hci_pcb *)so->so_pcb;
	struct sockaddr_bt *sa;

	KKASSERT(nam != NULL);
	sa = (struct sockaddr_bt *)nam;

	memset(sa, 0, sizeof(struct sockaddr_bt));
	sa->bt_len = sizeof(struct sockaddr_bt);
	sa->bt_family = AF_BLUETOOTH;
	bdaddr_copy(&sa->bt_bdaddr, &pcb->hp_raddr);

	lwkt_replymsg(&msg->connect.base.lmsg, 0);
}

static void
hci_ssockaddr(netmsg_t msg)
{
	struct socket *so = msg->sockaddr.base.nm_so;
	struct sockaddr **nam = msg->sockaddr.nm_nam;
	struct hci_pcb *pcb = (struct hci_pcb *)so->so_pcb;
	struct sockaddr_bt *sa;
	
	KKASSERT(nam != NULL);
	sa = (struct sockaddr_bt *)nam;

	memset(sa, 0, sizeof(struct sockaddr_bt));
	sa->bt_len = sizeof(struct sockaddr_bt);
	sa->bt_family = AF_BLUETOOTH;
	bdaddr_copy(&sa->bt_bdaddr, &pcb->hp_laddr);

	lwkt_replymsg(&msg->connect.base.lmsg, 0);
}

static void
hci_sshutdown(netmsg_t msg)
{
	struct socket *so = msg->shutdown.base.nm_so;

	socantsendmore(so);
	lwkt_replymsg(&msg->connect.base.lmsg, 0);
}

static void
hci_ssend(netmsg_t msg)
{
	struct socket *so = msg->send.base.nm_so;
	struct mbuf *m = msg->send.nm_m;
	struct sockaddr *addr = msg->send.nm_addr;
	struct mbuf *control = msg->send.nm_control;
	struct hci_pcb *pcb = (struct hci_pcb *)so->so_pcb;
	struct sockaddr_bt *sa;
	int error;

	sa = NULL;
	if (addr) {
		sa = (struct sockaddr_bt *)addr;

		if (sa->bt_len != sizeof(struct sockaddr_bt)) {
			error = EINVAL;
			goto out;
		}

		if (sa->bt_family != AF_BLUETOOTH) {
			error = EAFNOSUPPORT;
			goto out;
		}
	}

	/* have no use for this */
	if (control) {
		m_freem(control);
		control = NULL;
	}
	error = hci_send(pcb, m, (sa ? &sa->bt_bdaddr : &pcb->hp_raddr));
	m = NULL;

out:
	if (m)
		m_freem(m);
	if (control)
		m_freem(control);
	lwkt_replymsg(&msg->send.base.lmsg, error);
}

/*
 * get/set socket options
 */
void
hci_ctloutput(netmsg_t msg)
{
	struct socket *so = msg->ctloutput.base.nm_so;
	struct sockopt *sopt = msg->ctloutput.nm_sopt;
	struct hci_pcb *pcb = (struct hci_pcb *)so->so_pcb;
	int idir = 0;
	int error = 0;

#ifdef notyet			/* XXX */
	DPRINTFN(2, "req %s\n", prcorequests[req]);
#endif

	if (pcb == NULL) {
		error = EINVAL;
		goto out;
	}

	if (sopt->sopt_level != BTPROTO_HCI) {
		error = ENOPROTOOPT;
		goto out;
	}

	switch(sopt->sopt_dir) {
	case PRCO_GETOPT:
		switch (sopt->sopt_name) {
		case SO_HCI_EVT_FILTER:
			soopt_from_kbuf(sopt, &pcb->hp_efilter,
			    sizeof(struct hci_filter));
			break;

		case SO_HCI_PKT_FILTER:
                        soopt_from_kbuf(sopt, &pcb->hp_pfilter,
			    sizeof(struct hci_filter));
			break;

		case SO_HCI_DIRECTION:
			if (pcb->hp_flags & HCI_DIRECTION)
				idir = 1;
			else
				idir = 0;
			soopt_from_kbuf(sopt, &idir, sizeof(int));
			break;

		default:
			error = ENOPROTOOPT;
			break;
		}
		break;

	case PRCO_SETOPT:
		switch (sopt->sopt_name) {
		case SO_HCI_EVT_FILTER:	/* set event filter */
			error = soopt_to_kbuf(sopt, &pcb->hp_efilter,
			    sizeof(struct hci_filter),
			    sizeof(struct hci_filter)); 
			break;

		case SO_HCI_PKT_FILTER:	/* set packet filter */
			error = soopt_to_kbuf(sopt, &pcb->hp_pfilter,
					      sizeof(struct hci_filter),
					      sizeof(struct hci_filter));
			break;

		case SO_HCI_DIRECTION:	/* request direction ctl messages */
			error = soopt_to_kbuf(sopt, &idir, sizeof(int),
					      sizeof(int));
			if (error)
				break;
			if (idir)
				pcb->hp_flags |= HCI_DIRECTION;
			else
				pcb->hp_flags &= ~HCI_DIRECTION;
			break;

		default:
			error = ENOPROTOOPT;
			break;
		}
		break;

	default:
		error = ENOPROTOOPT;
		break;
	}
out:
	lwkt_replymsg(&msg->ctloutput.base.lmsg, error);
}

/*
 * HCI mbuf tap routine
 *
 * copy packets to any raw HCI sockets that wish (and are
 * permitted) to see them
 */
void
hci_mtap(struct mbuf *m, struct hci_unit *unit)
{
	struct hci_pcb *pcb;
	struct mbuf *m0, *ctlmsg, **ctl;
	struct sockaddr_bt sa;
	uint8_t type;
	uint8_t event;
	uint16_t opcode;

	KKASSERT(m->m_len >= sizeof(type));

	type = *mtod(m, uint8_t *);

	memset(&sa, 0, sizeof(sa));
	sa.bt_len = sizeof(struct sockaddr_bt);
	sa.bt_family = AF_BLUETOOTH;
	bdaddr_copy(&sa.bt_bdaddr, &unit->hci_bdaddr);

	LIST_FOREACH(pcb, &hci_pcb, hp_next) {
		/*
		 * filter according to source address
		 */
		if ((pcb->hp_flags & HCI_PROMISCUOUS) == 0
		    && bdaddr_same(&pcb->hp_laddr, &sa.bt_bdaddr) == 0)
			continue;

		/*
		 * filter according to packet type filter
		 */
		if (hci_filter_test(type, &pcb->hp_pfilter) == 0)
			continue;

		/*
		 * filter according to event/security filters
		 */
		switch(type) {
		case HCI_EVENT_PKT:
			KKASSERT(m->m_len >= sizeof(hci_event_hdr_t));

			event = mtod(m, hci_event_hdr_t *)->event;

			if (hci_filter_test(event, &pcb->hp_efilter) == 0)
				continue;

			if ((pcb->hp_flags & HCI_PRIVILEGED) == 0
			    && hci_security_check_event(event) == -1)
				continue;
			break;

		case HCI_CMD_PKT:
			KKASSERT(m->m_len >= sizeof(hci_cmd_hdr_t));

			opcode = letoh16(mtod(m, hci_cmd_hdr_t *)->opcode);

			if ((pcb->hp_flags & HCI_PRIVILEGED) == 0
			    && hci_security_check_opcode(NULL, opcode) == -1)
				continue;
			break;

		case HCI_ACL_DATA_PKT:
		case HCI_SCO_DATA_PKT:
		default:
			if ((pcb->hp_flags & HCI_PRIVILEGED) == 0)
				continue;

			break;
		}

		/*
		 * create control messages
		 */
		ctlmsg = NULL;
		ctl = &ctlmsg;
		if (pcb->hp_flags & HCI_DIRECTION) {
			int dir = m->m_flags & IFF_LINK0 ? 1 : 0;

			*ctl = sbcreatecontrol((void *)&dir, sizeof(dir),
			    SCM_HCI_DIRECTION, BTPROTO_HCI);

			if (*ctl != NULL)
				ctl = &((*ctl)->m_next);
		}

		/*
		 * copy to socket
		 */
		m0 = m_copym(m, 0, M_COPYALL, M_NOWAIT);
		if (m0 && sbappendaddr(&pcb->hp_socket->so_rcv.sb,
				(struct sockaddr *)&sa, m0, ctlmsg)) {
			sorwakeup(pcb->hp_socket);
		} else {
			m_freem(ctlmsg);
			m_freem(m0);
		}
	}
}

struct pr_usrreqs hci_usrreqs = {
        .pru_abort = hci_sabort,
        .pru_accept = pr_generic_notsupp,
        .pru_attach = hci_sattach,
        .pru_bind = hci_sbind,
        .pru_connect = hci_sconnect,
        .pru_connect2 = pr_generic_notsupp,
        .pru_control = hci_scontrol,
        .pru_detach = hci_sdetach,
        .pru_disconnect = hci_sdisconnect,
        .pru_listen = pr_generic_notsupp,
        .pru_peeraddr = hci_speeraddr,
        .pru_rcvd = pr_generic_notsupp,
        .pru_rcvoob = pr_generic_notsupp,
        .pru_send = hci_ssend,
        .pru_sense = pru_sense_null,
        .pru_shutdown = hci_sshutdown,
        .pru_sockaddr = hci_ssockaddr,
        .pru_sosend = sosend,
        .pru_soreceive = soreceive
};
