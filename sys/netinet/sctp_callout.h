/*	$KAME: sctp_callout.h,v 1.8 2005/01/25 07:35:42 itojun Exp $	*/
/*	$DragonFly: src/sys/netinet/sctp_callout.h,v 1.3 2006/05/20 02:42:12 dillon Exp $	*/

#ifndef _NETINET_SCTP_CALLOUT_H_
#define _NETINET_SCTP_CALLOUT_H_

#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif

#error "sctp_callout.h should never be used by dragonfly"

/*
 * Copyright (C) 2002, 2003, 2004 Cisco Systems Inc,
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#define _SCTP_NEEDS_CALLOUT_ 1

#ifndef __NetBSD__
struct callout {
	TAILQ_ENTRY(callout) tqe;
	int	c_time;				/* ticks to the event */
	void	*c_arg;				/* function argument */
	void	(*c_func)(void *);		/* function to call */
	int	c_flags;			/* state of this entry */
};
#endif
#define SCTP_TICKS_PER_FASTTIMO 20		/* we get called about */
                                                /* every 20ms */

TAILQ_HEAD(calloutlist, callout);

#define	CALLOUT_ACTIVE		0x0002 /* callout is currently active */
#ifndef __NetBSD__
#define	CALLOUT_PENDING		0x0004 /* callout is waiting for timeout */
#define CALLOUT_FIRED		0x0008 /* it expired */
#endif

#define	callout_active(c)	((c)->c_flags & CALLOUT_ACTIVE)
#define	callout_deactivate(c)	((c)->c_flags &= ~CALLOUT_ACTIVE)
void	callout_init(struct callout *);
#define	callout_pending(c)	((c)->c_flags & CALLOUT_PENDING)

void	callout_reset(struct callout *, int, void (*)(void *), void *);
#ifndef __NetBSD__
int	callout_stop(struct callout *);
#endif
#endif
