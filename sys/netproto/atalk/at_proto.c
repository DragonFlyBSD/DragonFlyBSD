/*
 * Copyright (c) 1990,1991 Regents of The University of Michigan.
 * All Rights Reserved.
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby granted,
 * provided that the above copyright notice appears in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation, and that the name of The University
 * of Michigan not be used in advertising or publicity pertaining to
 * distribution of the software without specific, written prior
 * permission. This software is supplied as is without expressed or
 * implied warranties of any kind.
 *
 *	Research Systems Unix Group
 *	The University of Michigan
 *	c/o Mike Clark
 *	535 W. William Street
 *	Ann Arbor, Michigan
 *	+1-313-763-0525
 *	netatalk@itd.umich.edu
 *
 * $DragonFly: src/sys/netproto/atalk/at_proto.c,v 1.7 2008/11/01 04:22:15 sephe Exp $
 */

#include <sys/param.h>
#include <sys/protosw.h>
#include <sys/domain.h>
#include <sys/socket.h>

#include <sys/kernel.h>

#include <net/route.h>

#include "at.h"
#include "ddp_var.h"
#include "at_extern.h"

static struct domain atalkdomain;

static struct protosw atalksw[] = {
    {
	/* Identifiers */
	SOCK_DGRAM,	&atalkdomain,	ATPROTO_DDP,	PR_ATOMIC|PR_ADDR,
	/*
	 * protocol-protocol interface.
	 * fields are pr_input, pr_output, pr_ctlinput, and pr_ctloutput.
	 * pr_input can be called from the udp protocol stack for iptalk
	 * packets bound for a local socket.
	 * pr_output can be used by higher level appletalk protocols, should
	 * they be included in the kernel.
	 */
	0,		ddp_output,	0,		0,
	cpu0_soport,	NULL,
	/* utility routines. */
	ddp_init,	0,		0,		0,
	&ddp_usrreqs
    },
};

static struct domain atalkdomain = {
	.dom_family = AF_APPLETALK,
	.dom_name = "appletalk",
	.dom_init = NULL,
	.dom_internalize = NULL,
	.dom_externalize = NULL,
	.dom_dispose = NULL,
	.dom_protosw = atalksw,
	.dom_protoswNPROTOSW = &atalksw[sizeof(atalksw)/sizeof(atalksw[0])],
	.dom_next = SLIST_ENTRY_INITIALIZER,
	.dom_rtattach = rn_inithead,
	.dom_rtoffset = 8 * offsetof(struct sockaddr_at, sat_addr),
	.dom_maxrtkey = sizeof(struct sockaddr_at)
};

DOMAIN_SET(atalk);

