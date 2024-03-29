/* $OpenBSD: src/sys/netbt/hci_unit.c,v 1.8 2008/02/24 21:34:48 uwe Exp $ */
/* $NetBSD: hci_unit.c,v 1.9 2007/12/30 18:26:42 plunky Exp $ */

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

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/systm.h>
#include <sys/endian.h>
#include <sys/bus.h>

#include <net/netisr.h>

#include <netbt/bluetooth.h>
#include <netbt/hci.h>

struct hci_unit_list hci_unit_list = TAILQ_HEAD_INITIALIZER(hci_unit_list);

/*
 * HCI Input Queue max lengths.
 */
int hci_eventq_max = 20;
int hci_aclrxq_max = 50;
int hci_scorxq_max = 50;
int hci_cmdwait_max = 50;
int hci_scodone_max = 50;

/*
 * This is the default minimum command set supported by older
 * devices. Anything conforming to 1.2 spec or later will get
 * updated during init.
 */
static const uint8_t hci_cmds_v10[HCI_COMMANDS_SIZE] = {
	0xff, 0xff, 0xff, 0x01, 0xfe, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0x7f, 0x32, 0x03, 0xb8, 0xfe,
	0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/*
 * bluetooth unit functions
 */

struct hci_unit *
hci_attach(const struct hci_if *hci_if, device_t dev, uint16_t flags)
{
	struct hci_unit *unit;

	KKASSERT(dev != NULL);
	KKASSERT(hci_if->enable != NULL);
	KKASSERT(hci_if->disable != NULL);
	KKASSERT(hci_if->output_cmd != NULL);
	KKASSERT(hci_if->output_acl != NULL);
	KKASSERT(hci_if->output_sco != NULL);
	KKASSERT(hci_if->get_stats != NULL);

	unit = kmalloc(sizeof(struct hci_unit), M_BLUETOOTH, M_ZERO | M_WAITOK);
	KKASSERT(unit != NULL);

	unit->hci_dev = dev;
	unit->hci_if = hci_if;
	unit->hci_flags = flags;

	lockinit(&unit->hci_devlock, "HCI device lock", 0, 0);

	unit->hci_eventq.ifq_maxlen = hci_eventq_max;
	unit->hci_aclrxq.ifq_maxlen = hci_aclrxq_max;
	unit->hci_scorxq.ifq_maxlen = hci_scorxq_max;
	unit->hci_cmdwait.ifq_maxlen = hci_cmdwait_max;
	unit->hci_scodone.ifq_maxlen = hci_scodone_max;

	TAILQ_INIT(&unit->hci_links);
	LIST_INIT(&unit->hci_memos);

	crit_enter();
	TAILQ_INSERT_TAIL(&hci_unit_list, unit, hci_next);
	crit_exit();

	return unit;
}

void
hci_detach(struct hci_unit *unit)
{
	crit_enter();
	hci_disable(unit);

	TAILQ_REMOVE(&hci_unit_list, unit, hci_next);
	crit_exit();

	kfree(unit, M_BLUETOOTH);
}

int
hci_enable(struct hci_unit *unit)
{
	int err;

	/*
	 * Bluetooth spec says that a device can accept one
	 * command on power up until they send a Command Status
	 * or Command Complete event with more information, but
	 * it seems that some devices cant and prefer to send a
	 * No-op Command Status packet when they are ready.
	 */
	unit->hci_num_cmd_pkts = (unit->hci_flags & BTF_POWER_UP_NOOP) ? 0 : 1;
	unit->hci_num_acl_pkts = 0;
	unit->hci_num_sco_pkts = 0;

	/*
	 * only allow the basic packet types until
	 * the features report is in
	 */
	unit->hci_acl_mask = HCI_PKT_DM1 | HCI_PKT_DH1;
	unit->hci_packet_type = unit->hci_acl_mask;

	memcpy(unit->hci_cmds, hci_cmds_v10, HCI_COMMANDS_SIZE);
	err = (*unit->hci_if->enable)(unit->hci_dev);
	if (err)
		goto bad1;

	unit->hci_flags |= BTF_RUNNING;

	/*
	 * Reset the device, this will trigger initialisation
	 * and wake us up.
	 */
	crit_enter();
 	unit->hci_flags |= BTF_INIT;
	crit_exit();

	err = hci_send_cmd(unit, HCI_CMD_RESET, NULL, 0);
	if (err)
		goto bad2;

	while (unit->hci_flags & BTF_INIT) {
		err = tsleep(unit, PCATCH, "hciena", 5 * hz);
		if (err)
			goto bad2;

		/* XXX
		 * "What If", while we were sleeping, the device
		 * was removed and detached? Ho Hum.
		 */
	}

#if 0 /* not yet */
	/*
	 * Attach Bluetooth Device Hub
	 */
	unit->hci_bthub = NULL;

	unit->hci_bthub = device_add_child(unit->hci_softc, "bthub", -1);
	if (!unit->hci_bthub) {
		device_printf(unit->hci_softc, "Device creation failed\n");
		goto bad2;
	}

	DPRINTFN(10, "%s is added as child to %s\n",
	    device_get_nameunit(unit->hci_bthub),
	    device_get_nameunit(unit->hci_softc));

	device_set_desc(unit->hci_bthub,"Bluetooth Device Hub");

	device_set_ivars(unit->hci_bthub, &unit->hci_bdaddr);

	device_probe_and_attach(unit->hci_bthub);
#endif
	return 0;

bad2:
	(*unit->hci_if->disable)(unit->hci_dev);
	unit->hci_flags &= ~BTF_RUNNING;

bad1:
	return err;
}

void
hci_disable(struct hci_unit *unit)
{
	struct hci_link *link, *next;
	struct hci_memo *memo;
	int acl;

#if 0 /* not yet */
	if (unit->hci_bthub) {
		device_delete_child(unit->hci_softc, unit->hci_bthub);
		unit->hci_bthub = NULL;
	}
#endif

	(*unit->hci_if->disable)(unit->hci_dev);
	unit->hci_flags &= ~BTF_RUNNING;

	/*
	 * close down any links, take care to close SCO first since
	 * they may depend on ACL links.
	 */
	for (acl = 0 ; acl < 2 ; acl++) {
		next = TAILQ_FIRST(&unit->hci_links);
		while ((link = next) != NULL) {
			next = TAILQ_NEXT(link, hl_next);
			if (acl || link->hl_type != HCI_LINK_ACL)
				hci_link_free(link, ECONNABORTED);
		}
	}

	while ((memo = LIST_FIRST(&unit->hci_memos)) != NULL)
		hci_memo_free(memo);

	/* (no need to hold hci_devlock, the driver is disabled) */

	IF_DRAIN(&unit->hci_eventq);
	unit->hci_eventqlen = 0;

	IF_DRAIN(&unit->hci_aclrxq);
	unit->hci_aclrxqlen = 0;

	IF_DRAIN(&unit->hci_scorxq);
	unit->hci_scorxqlen = 0;

	IF_DRAIN(&unit->hci_cmdwait);
	IF_DRAIN(&unit->hci_scodone);
}

struct hci_unit *
hci_unit_lookup(bdaddr_t *addr)
{
	struct hci_unit *unit;

	TAILQ_FOREACH(unit, &hci_unit_list, hci_next) {
		if ((unit->hci_flags & BTF_UP) == 0)
			continue;

		if (bdaddr_same(&unit->hci_bdaddr, addr))
			break;
	}

	return unit;
}

/*
 * construct and queue a HCI command packet
 */
int
hci_send_cmd(struct hci_unit *unit, uint16_t opcode, void *buf, uint8_t len)
{
	struct mbuf *m;
	hci_cmd_hdr_t *p;

	KKASSERT(unit != NULL);

	m = m_gethdr(M_NOWAIT, MT_DATA);
	if (m == NULL)
		return ENOMEM;

	p = mtod(m, hci_cmd_hdr_t *);
	p->type = HCI_CMD_PKT;
	p->opcode = htole16(opcode);
	p->length = len;
	m->m_pkthdr.len = m->m_len = sizeof(hci_cmd_hdr_t);
	M_SETCTX(m, NULL);	/* XXX is this needed? */

	if (len > 0 &&
	    m_copyback2(m, sizeof(hci_cmd_hdr_t), len, buf, M_NOWAIT) != 0) {
		m_freem(m);
		return ENOMEM;
	}

	DPRINTFN(2, "(%s) opcode (%3.3x|%4.4x)\n",
		device_get_nameunit(unit->hci_dev),
		HCI_OGF(opcode), HCI_OCF(opcode));

	/* and send it on */
	if (unit->hci_num_cmd_pkts == 0)
		IF_ENQUEUE(&unit->hci_cmdwait, m);
	else
		hci_output_cmd(unit, m);

	return 0;
}

/*
 * Incoming packet processing. Since the code is single threaded
 * in any case (IPL_SOFTNET), we handle it all in one interrupt function
 * picking our way through more important packets first so that hopefully
 * we will never get clogged up with bulk data.
 */
void
hci_intr(void *arg)
{
	struct hci_unit *unit = arg;
	struct mbuf *m;

another:
	lockmgr(&unit->hci_devlock, LK_EXCLUSIVE);

	if (unit->hci_eventqlen > 0) {
		IF_DEQUEUE(&unit->hci_eventq, m);
		unit->hci_eventqlen--;
		lockmgr(&unit->hci_devlock, LK_RELEASE);

		KKASSERT(m != NULL);

		DPRINTFN(10, "(%s) recv event, len = %d\n",
		    device_get_nameunit(unit->hci_dev), m->m_pkthdr.len);

		m->m_flags |= IFF_LINK0;	/* mark incoming packet */
		hci_mtap(m, unit);
		hci_event(m, unit);

		goto another;
	}

	if (unit->hci_scorxqlen > 0) {
		IF_DEQUEUE(&unit->hci_scorxq, m);
		unit->hci_scorxqlen--;
		lockmgr(&unit->hci_devlock, LK_RELEASE);
		KKASSERT(m != NULL);

		DPRINTFN(10, "(%s) recv SCO, len = %d\n",
		    device_get_nameunit(unit->hci_dev), m->m_pkthdr.len);

		m->m_flags |= IFF_LINK0;	/* mark incoming packet */
		hci_mtap(m, unit);
		hci_sco_recv(m, unit);

		goto another;
	}

	if (unit->hci_aclrxqlen > 0) {
		IF_DEQUEUE(&unit->hci_aclrxq, m);
		unit->hci_aclrxqlen--;
		lockmgr(&unit->hci_devlock, LK_RELEASE);

		KKASSERT(m != NULL);

		DPRINTFN(10, "(%s) recv ACL, len = %d\n",
		    device_get_nameunit(unit->hci_dev), m->m_pkthdr.len);

		m->m_flags |= IFF_LINK0;	/* mark incoming packet */
		hci_mtap(m, unit);
		hci_acl_recv(m, unit);

		goto another;
	}

	IF_DEQUEUE(&unit->hci_scodone, m);
	if (m != NULL) {
		struct hci_link *link;
		lockmgr(&unit->hci_devlock, LK_RELEASE);

		DPRINTFN(11, "(%s) complete SCO\n",
		    device_get_nameunit(unit->hci_dev));

		TAILQ_FOREACH(link, &unit->hci_links, hl_next) {
			if (link == M_GETCTX(m, struct hci_link *)) {
				hci_sco_complete(link, 1);
				break;
			}
		}

		unit->hci_num_sco_pkts++;
		m_freem(m);

		goto another;
	}

	lockmgr(&unit->hci_devlock, LK_RELEASE);

	DPRINTFN(10, "done\n");
}

/**********************************************************************
 *
 * IO routines
 *
 * input & complete routines will be called from device drivers,
 * possibly in interrupt context. We return success or failure to
 * enable proper accounting but we own the mbuf.
 */

int
hci_input_event(struct hci_unit *unit, struct mbuf *m)
{
	int rv;

	lockmgr(&unit->hci_devlock, LK_EXCLUSIVE);

	if (unit->hci_eventqlen > hci_eventq_max) {
		DPRINTF("(%s) dropped event packet.\n",
		    device_get_nameunit(unit->hci_dev));
		m_freem(m);
		rv = 0;
	} else {
		unit->hci_eventqlen++;
		IF_ENQUEUE(&unit->hci_eventq, m);
		netisr_queue(NETISR_BLUETOOTH, m);
		rv = 1;
	}
	lockmgr(&unit->hci_devlock, LK_RELEASE);

	return rv;
}

int
hci_input_acl(struct hci_unit *unit, struct mbuf *m)
{
	int rv;

	lockmgr(&unit->hci_devlock, LK_EXCLUSIVE);
	if (unit->hci_aclrxqlen > hci_aclrxq_max) {
		DPRINTF("(%s) dropped ACL packet.\n",
		    device_get_nameunit(unit->hci_dev));
		m_freem(m);
		rv = 0;
	} else {
		unit->hci_aclrxqlen++;
		IF_ENQUEUE(&unit->hci_aclrxq, m);
		netisr_queue(NETISR_BLUETOOTH, m);
		rv = 1;
	}
	lockmgr(&unit->hci_devlock, LK_RELEASE);

	return rv;
}

int
hci_input_sco(struct hci_unit *unit, struct mbuf *m)
{
	int rv;

	lockmgr(&unit->hci_devlock, LK_EXCLUSIVE);
	if (unit->hci_scorxqlen > hci_scorxq_max) {
		DPRINTF("(%s) dropped SCO packet.\n",
		    device_get_nameunit(unit->hci_dev));
		m_freem(m);
		rv = 0;
	} else {
		unit->hci_scorxqlen++;
		IF_ENQUEUE(&unit->hci_scorxq, m);
		netisr_queue(NETISR_BLUETOOTH, m);
		rv = 1;
	}
	lockmgr(&unit->hci_devlock, LK_RELEASE);

	return rv;
}

void
hci_output_cmd(struct hci_unit *unit, struct mbuf *m)
{
	void *arg;

	hci_mtap(m, unit);

	DPRINTFN(10, "(%s) num_cmd_pkts=%d\n",
	    device_get_nameunit(unit->hci_dev), unit->hci_num_cmd_pkts);

	unit->hci_num_cmd_pkts--;

	/*
	 * If context is set, this was from a HCI raw socket
	 * and a record needs to be dropped from the sockbuf.
	 */
	arg = M_GETCTX(m, void *);
	if (arg != NULL)
		hci_drop(arg);

	(*unit->hci_if->output_cmd)(unit->hci_dev, m);
}

void
hci_output_acl(struct hci_unit *unit, struct mbuf *m)
{
	hci_mtap(m, unit);

	DPRINTFN(10, "(%s) num_acl_pkts=%d\n",
	    device_get_nameunit(unit->hci_dev), unit->hci_num_acl_pkts);

	unit->hci_num_acl_pkts--;

	(*unit->hci_if->output_acl)(unit->hci_dev, m);
}

void
hci_output_sco(struct hci_unit *unit, struct mbuf *m)
{
	hci_mtap(m, unit);

	DPRINTFN(10, "(%s) num_sco_pkts=%d\n",
	    device_get_nameunit(unit->hci_dev), unit->hci_num_sco_pkts);

	unit->hci_num_sco_pkts--;

	(*unit->hci_if->output_sco)(unit->hci_dev, m);
}

int
hci_complete_sco(struct hci_unit *unit, struct mbuf *m)
{
	lockmgr(&unit->hci_devlock, LK_EXCLUSIVE);
	IF_ENQUEUE(&unit->hci_scodone, m);
	netisr_queue(NETISR_BLUETOOTH,m);
	lockmgr(&unit->hci_devlock, LK_RELEASE);

	return 1;
}
