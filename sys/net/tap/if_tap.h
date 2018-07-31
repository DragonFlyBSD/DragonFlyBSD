/*
 * Copyright (C) 1999-2000 by Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * BASED ON:
 * -------------------------------------------------------------------------
 *
 * Copyright (c) 1988, Julian Onions <jpo@cs.nott.ac.uk>
 * Nottingham University 1987.
 */

/*
 * $FreeBSD: src/sys/net/if_tap.h,v 1.1.2.1 2000/07/27 13:57:05 nsayer Exp $
 * $Id: if_tap.h,v 0.7 2000/07/12 04:12:51 max Exp $
 */

#ifndef _NET_IF_TAP_H_
#define _NET_IF_TAP_H_

#include <sys/ioccom.h>

/* maximum receive packet size (hard limit) */
#define	TAPMRU		16384

struct tapinfo {
	int	baudrate;	/* linespeed */
	short	mtu;		/* maximum transmission unit */
	u_char	type;		/* IFT_ETHER only */
	u_char	dummy;		/* place holder */
};

/* get/set internal debug variable */
#define	TAPGDEBUG		_IOR('t', 89, int)
#define	TAPSDEBUG		_IOW('t', 90, int)
/* get/set network interface information */
#define	TAPSIFINFO		_IOW('t', 91, struct tapinfo)
#define	TAPGIFINFO		_IOR('t', 92, struct tapinfo)
/* get the network interface name */
#define	TAPGIFNAME		_IOR('t', 93, struct ifreq)

#endif /* !_NET_IF_TAP_H_ */
