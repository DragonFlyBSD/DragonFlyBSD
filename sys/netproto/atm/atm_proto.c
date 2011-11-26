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
 *	@(#) $FreeBSD: src/sys/netatm/atm_proto.c,v 1.3 1999/08/28 00:48:36 peter Exp $
 */

/*
 * Core ATM Services
 * -----------------
 *
 * ATM socket protocol family support definitions
 *
 */

#include "kern_include.h"

struct protosw atmsw[] = {
    {
	.pr_type = SOCK_DGRAM,		/* ioctl()-only */
	.pr_domain = &atmdomain,
	.pr_protocol = 0,
	.pr_flags = 0,

	.pr_usrreqs = &atm_dgram_usrreqs
    },

    {
	.pr_type = SOCK_SEQPACKET,	/* AAL-5 */
	.pr_domain = &atmdomain,
	.pr_protocol = ATM_PROTO_AAL5,
	.pr_flags = PR_ATOMIC|PR_CONNREQUIRED,

	.pr_input = NULL,
	.pr_output = NULL,
	.pr_ctlinput = NULL,
	.pr_ctloutput = atm_aal5_ctloutput,

	.pr_usrreqs = &atm_aal5_usrreqs,
    },

#ifdef XXX
    {
	.pr_type = SOCK_SEQPACKET,	/* SSCOP */
	.pr_domain = &atmdomain,
	.pr_protocol = ATM_PROTO_SSCOP,
	.pr_flags = PR_ATOMIC|PR_CONNREQUIRED|PR_WANTRCVD,

	x,			/* pr_input */
	x,			/* pr_output */
	x,			/* pr_ctlinput */
	x,			/* pr_ctloutput */
	NULL,			/* pr_mport */
	NULL,			/* pr_ctlport */
	0,			/* pr_init */
	0,			/* pr_fasttimo */
	0,			/* pr_slowtimo */
	x,			/* pr_drain */
	x,			/* pr_usrreqs */
    },
#endif
};

struct domain atmdomain = {
	AF_ATM, "atm", atm_initialize, NULL, NULL,
	atmsw, &atmsw[NELEM(atmsw)],
};

DOMAIN_SET(atm);
