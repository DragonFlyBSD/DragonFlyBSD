/*	$KAME: altq.h,v 1.10 2003/07/10 12:07:47 kjc Exp $	*/
/*	$DragonFly: src/sys/net/altq/altq.h,v 1.2 2008/04/06 18:58:15 dillon Exp $ */

/*
 * Copyright (C) 1998-2003
 *	Sony Computer Science Laboratories Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY SONY CSL AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL SONY CSL OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifndef _ALTQ_ALTQ_H_
#define	_ALTQ_ALTQ_H_

/* altq discipline type */
#define	ALTQT_NONE		0	/* reserved */
#define	ALTQT_CBQ		1	/* cbq */
#define	ALTQT_RED		2	/* red */
#define	ALTQT_RIO		3	/* rio */
#define	ALTQT_HFSC		4	/* hfsc */
#define	ALTQT_PRIQ		5	/* priority queue */
#define	ALTQT_FAIRQ		6	/* fair queue (requires keep state) */
#define	ALTQT_MAX		7	/* should be max discipline type + 1 */

/* simple token packet meter profile */
struct	tb_profile {
	u_int	rate;	/* rate in bit-per-sec */
	u_int	depth;	/* depth in bytes */
};

/*
 * generic packet counter
 */
struct pktcntr {
	uint64_t	packets;
	uint64_t	bytes;
};

#define	PKTCNTR_ADD(cntr, len)		do { 				\
	(cntr)->packets++; (cntr)->bytes += len;			\
} while (0)

#ifdef _KERNEL
#include <net/altq/altq_var.h>
#endif

#endif /* _ALTQ_ALTQ_H_ */
