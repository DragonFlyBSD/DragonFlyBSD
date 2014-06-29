/*
 * Copyright (c) 1997, 1998, 1999, 2000
 *	Bill Paul <wpaul@ee.columbia.edu>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * $FreeBSD: src/sys/dev/usb/usb_ethersubr.c,v 1.17 2003/11/14 11:09:45 johan Exp $
 * $DragonFly: src/sys/bus/usb/usb_ethersubr.c,v 1.21 2008/09/24 14:26:38 sephe Exp $
 */

/*
 * Callbacks in the USB code operate in a critical section.
 *
 * It is conceivable that this arrangement could trigger a condition
 * where the input queues could get trampled in spite of our best effors
 * to prevent it. To work around this, we implement a special input queue
 * for USB ethernet adapter drivers. Rather than passing the frames directly
 * to ether_input(), we pass them here, then schedule a soft interrupt to
 * hand them to ether_input() later, outside of the USB interrupt context.
 *
 * It's questional as to whether this code should be expanded to
 * handle other kinds of devices, or handle USB transfer callbacks
 * in general. Right now, I need USB network interfaces to work
 * properly.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/socket.h>

#include <sys/thread2.h>
#include <sys/msgport2.h>
#include <sys/mplock2.h>

#include <net/if.h>
#include <net/ifq_var.h>
#include <net/if_types.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/netisr.h>
#include <net/bpf.h>

#include "usb.h"
#include "usb_ethersubr.h"

static int netisr_inited = 0;

static void
usbintr(netmsg_t msg)
{
	struct mbuf *m = msg->packet.nm_packet;
	struct ifnet *ifp;

	/* not MPSAFE */
	get_mplock();
	ifp = m->m_pkthdr.rcvif;
	ifp->if_input(ifp, m, NULL, -1);
	/* the msg is embedded in the mbuf, do not reply it */
	rel_mplock();
}

void
usb_register_netisr(void)
{
	if (netisr_inited == 0) {
		netisr_inited = 1;
		netisr_register(NETISR_USB, usbintr, NULL);
	}
}

/*
 * Must be called from a critical section.  This should be the case when
 * called from a transfer callback routine.  Don't trust it, though.
 */
void
usb_ether_input(struct mbuf *m)
{
	crit_enter();
	netisr_queue(NETISR_USB, m);
	crit_exit();
}
