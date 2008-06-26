/*-
 * Copyright (c) 2003-2004
 *	Hartmut Brandt
 * 	All rights reserved.
 *
 * Author: Hartmut Brandt <harti@freebsd.org>
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
 * Customisation of call control source to the NG environment.
 *
 * $FreeBSD: src/sys/netgraph/atm/ccatm/ng_ccatm_cust.h,v 1.2 2005/01/07 01:45:41 imp Exp $
 * $DragonFly: src/sys/netgraph7/atm/ccatm/ng_ccatm_cust.h,v 1.2 2008/06/26 23:05:39 dillon Exp $
 * $DragonFly: src/sys/netgraph7/atm/ccatm/ng_ccatm_cust.h,v 1.2 2008/06/26 23:05:39 dillon Exp $ 
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include "ng_message.h"
#include "netgraph.h"
#include "atm/ngatmbase.h"

#define	CCASSERT(E, M) KASSERT(E, M)

MALLOC_DECLARE(M_NG_CCATM);

#define	CCMALLOC(S)	(kmalloc((S), M_NG_CCATM, M_WAITOK | M_NULLOK))
#define	CCZALLOC(S)	(kmalloc((S), M_NG_CCATM, M_WAITOK | M_NULLOK | M_ZERO))
#define	CCFREE(P)	do { kfree((P), M_NG_CCATM); } while (0)

#define	CCGETERRNO()	(ENOMEM)
