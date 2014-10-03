/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
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

#include <sys/domain.h>
#include <sys/kernel.h>		/* SYSINIT via DOMAIN_SET */
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/globaldata.h>
#include <sys/thread.h>

#include <net/radix.h>		/* rn_inithead */

#include <netproto/mpls/mpls.h>
#include <netproto/mpls/mpls_var.h>

/* forward declarations */
static struct domain mplsdomain;
static struct pr_usrreqs nousrreqs;  /* XXX use this for something */

struct protosw mplssw[] = {
    {
	.pr_type = 0,
	.pr_domain = &mplsdomain,

	.pr_init = mpls_init,
	.pr_usrreqs = &nousrreqs
    },
    {
	.pr_type = SOCK_RAW,
	.pr_domain = &mplsdomain,
	.pr_protocol = 0,
	.pr_flags = PR_ATOMIC|PR_ADDR,

	.pr_usrreqs = &nousrreqs
    }
};

static int
mpls_inithead(void **head, int off)
{
	return rn_inithead(head, rn_cpumaskhead(mycpuid), off);
}

static	struct	domain mplsdomain = {
	AF_MPLS,			/* dom_family */
	"mpls",				/* dom_name */
	NULL,				/* dom_init */
	NULL,				/* dom_externalize */
	NULL,				/* dom_dispose */
	mplssw,				/* dom_protosw */
	&mplssw[NELEM(mplssw)],
					/* dom_protoswNPROTOSW */
	SLIST_ENTRY_INITIALIZER,	/* dom_next */
	mpls_inithead,			/* dom_rtattach */
	32,				/* dom_rtoffset */
	sizeof(struct sockaddr_mpls),	/* dom_maxrtkey */
	NULL,				/* dom_ifattach */
	NULL				/* dom_ifdetach */
};

DOMAIN_SET(mpls);

#if 0
SYSCTL_NODE(_net,	PF_MPLS,	mpls,	CTLFLAG_RW,	0,
	"MPLS Family");
#endif
