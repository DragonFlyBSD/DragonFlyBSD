/*	$FreeBSD: src/sys/netkey/keydb.c,v 1.1.2.1 2000/07/15 07:14:42 kris Exp $	*/
/*	$KAME: keydb.c,v 1.64 2000/05/11 17:02:30 itojun Exp $	*/

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/errno.h>
#include <sys/queue.h>
#include <sys/thread2.h>

#include <net/if.h>
#include <net/route.h>

#include <netinet/in.h>

#include <net/pfkeyv2.h>
#include "keydb.h"
#include "key.h"
#include <netinet6/ipsec.h>

#include <net/net_osdep.h>

MALLOC_DEFINE(M_SECA, "key mgmt", "security associations, key management");

static void keydb_delsecasvar (struct secasvar *);

/*
 * secpolicy management
 */
struct secpolicy *
keydb_newsecpolicy(void)
{
	return(kmalloc(sizeof(struct secpolicy), M_SECA,
		       M_INTWAIT | M_NULLOK | M_ZERO));
}

void
keydb_delsecpolicy(struct secpolicy *p)
{
	kfree(p, M_SECA);
}

/*
 * secashead management
 */
struct secashead *
keydb_newsecashead(void)
{
	struct secashead *p;
	int i;

	p = kmalloc(sizeof(*p), M_SECA, M_INTWAIT | M_NULLOK | M_ZERO);
	if (!p)
		return p;
	for (i = 0; i < NELEM(p->savtree); i++)
		LIST_INIT(&p->savtree[i]);
	return p;
}

void
keydb_delsecashead(struct secashead *p)
{

	kfree(p, M_SECA);
}

/*
 * secasvar management (reference counted)
 */
struct secasvar *
keydb_newsecasvar(void)
{
	struct secasvar *p;

	p = kmalloc(sizeof(*p), M_SECA, M_INTWAIT | M_NULLOK | M_ZERO);
	if (!p)
		return p;
	p->refcnt = 1;
	return p;
}

void
keydb_refsecasvar(struct secasvar *p)
{
	lwkt_gettoken(&key_token);
	p->refcnt++;
	lwkt_reltoken(&key_token);
}

void
keydb_freesecasvar(struct secasvar *p)
{
	lwkt_gettoken(&key_token);
	p->refcnt--;
	/* negative refcnt will cause panic intentionally */
	if (p->refcnt <= 0)
		keydb_delsecasvar(p);
	lwkt_reltoken(&key_token);
}

static void
keydb_delsecasvar(struct secasvar *p)
{

	if (p->refcnt)
		panic("keydb_delsecasvar called with refcnt != 0");

	kfree(p, M_SECA);
}

/*
 * secreplay management
 */
struct secreplay *
keydb_newsecreplay(size_t wsize)
{
	struct secreplay *p;

	p = kmalloc(sizeof(*p), M_SECA, M_INTWAIT | M_NULLOK | M_ZERO);
	if (!p)
		return p;

	if (wsize != 0) {
		p->bitmap = (caddr_t)kmalloc(wsize, M_SECA, M_INTWAIT | M_NULLOK | M_ZERO);
		if (!p->bitmap) {
			kfree(p, M_SECA);
			return NULL;
		}
	}
	p->wsize = wsize;
	return p;
}

void
keydb_delsecreplay(struct secreplay *p)
{

	if (p->bitmap)
		kfree(p->bitmap, M_SECA);
	kfree(p, M_SECA);
}

/*
 * secreg management
 */
struct secreg *
keydb_newsecreg(void)
{
	struct secreg *p;

	p = kmalloc(sizeof(*p), M_SECA, M_INTWAIT | M_ZERO | M_NULLOK);
	return p;
}

void
keydb_delsecreg(struct secreg *p)
{

	kfree(p, M_SECA);
}
