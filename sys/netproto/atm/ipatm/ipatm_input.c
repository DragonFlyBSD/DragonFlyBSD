/*
 *
 * ===================================
 * HARP  |  Host ATM Research Platform
 * ===================================
 *
 *
 * This Host ATM Research Platform ("HARP") file (the "Software") is
 * made available by Network Computing Services, Inc. ("NetworkCS")
 * "AS IS".  NetworkCS does not provide maintenance, improvements or
 * support of any kind.
 *
 * NETWORKCS MAKES NO WARRANTIES OR REPRESENTATIONS, EXPRESS OR IMPLIED,
 * INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE, AS TO ANY ELEMENT OF THE
 * SOFTWARE OR ANY SUPPORT PROVIDED IN CONNECTION WITH THIS SOFTWARE.
 * In no event shall NetworkCS be responsible for any damages, including
 * but not limited to consequential damages, arising from or relating to
 * any use of the Software or related support.
 *
 * Copyright 1994-1998 Network Computing Services, Inc.
 *
 * Copies of this Software may be made, however, the above copyright
 * notice must be reproduced on all copies.
 *
 *	@(#) $FreeBSD: src/sys/netatm/ipatm/ipatm_input.c,v 1.4 2000/01/17 20:49:43 mks Exp $
 *	@(#) $DragonFly: src/sys/netproto/atm/ipatm/ipatm_input.c,v 1.7 2006/01/14 13:36:39 swildner Exp $
 */

/*
 * IP Over ATM Support
 * -------------------
 *
 * Process stack and data input
 *
 */

#include <netproto/atm/kern_include.h>

#include "ipatm_var.h"

/*
 * Process VCC Input Data
 * 
 * Arguments:
 *	tok	ipatm connection token (pointer to ipvcc)
 *	m	pointer to input packet buffer chain
 *
 * Returns:
 *	none
 *
 */
void
ipatm_cpcs_data(void *tok, KBuffer *m)
{
	struct ipvcc	*ivp = tok;

#ifdef DIAGNOSTIC
	if (ipatm_print) {
		atm_pdu_print(m, "ipatm_input");
	}
#endif

	/*
	 * Handle input packet
	 */
	if (ivp->iv_state != IPVCC_ACTIVE) {
		KB_FREEALL(m);
		ipatm_stat.ias_rcvstate++;
		return;
	}

	/*
	 * IP packet - reset idle timer
	 */
	ivp->iv_idle = 0;

	/*
	 * Pass packet to IP
	 */
	ipatm_ipinput(ivp->iv_ipnif, m);
}


/*
 * IP Input Packet Handler
 * 
 * All IP packets received from various ATM sources will be sent here
 * for final queuing to the IP layer.
 *
 * Arguments:
 *	inp	pointer to packet's receiving IP network interface
 *	m	pointer to packet buffer chain
 *
 * Returns:
 *	0	packet successfully queued to IP layer
 *	else	error queuing packet, buffer chain freed
 *
 */
int
ipatm_ipinput(struct ip_nif *inp, KBuffer *m)
{
#ifdef DIAGNOSTIC
	if (ipatm_print) {
		atm_pdu_print(m, "ipatm_ipinput");
	}
#endif

#ifdef DIAGNOSTIC
	if (!KB_ISPKT(m)) {
		panic("ipatm_ipinput: no packet header");
	}
	{
		int	cnt = 0;
		KBuffer	*m0 = m;

		while (m0) {
			cnt += KB_LEN(m0);
			m0 = KB_NEXT(m0);
		}
		if (m->m_pkthdr.len != cnt) {
			panic("ipatm_ipinput: packet length incorrect");
		}
	}
#endif
	/*
	 * Save the input ifnet pointer in the packet header
	 */
	m->m_pkthdr.rcvif = (struct ifnet *)inp->inf_nif;

	/*
	 * Finally, hand packet off to IP.
	 *
	 * NB: Since we're already in the softint kernel state, we
	 * just call IP directly to avoid the extra unnecessary 
	 * kernel scheduling.
	 */
	netisr_queue(NETISR_IP, m);
	return (0);
}
