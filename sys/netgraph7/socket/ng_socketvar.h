/*
 * netgraph.h
 */

/*-
 * Copyright (c) 1996-1999 Whistle Communications, Inc.
 * All rights reserved.
 * 
 * Subject to the following obligations and disclaimer of warranty, use and
 * redistribution of this software, in source or object code forms, with or
 * without modifications are expressly permitted by Whistle Communications;
 * provided, however, that:
 * 1. Any and all reproductions of the source or object code must include the
 *    copyright notice above and the following disclaimer of warranties; and
 * 2. No rights are granted, in any manner or form, to use Whistle
 *    Communications, Inc. trademarks, including the mark "WHISTLE
 *    COMMUNICATIONS" on advertising, endorsements, or otherwise except as
 *    such appears in the above copyright notice or in the software.
 * 
 * THIS SOFTWARE IS BEING PROVIDED BY WHISTLE COMMUNICATIONS "AS IS", AND
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, WHISTLE COMMUNICATIONS MAKES NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED, REGARDING THIS SOFTWARE,
 * INCLUDING WITHOUT LIMITATION, ANY AND ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
 * WHISTLE COMMUNICATIONS DOES NOT WARRANT, GUARANTEE, OR MAKE ANY
 * REPRESENTATIONS REGARDING THE USE OF, OR THE RESULTS OF THE USE OF THIS
 * SOFTWARE IN TERMS OF ITS CORRECTNESS, ACCURACY, RELIABILITY OR OTHERWISE.
 * IN NO EVENT SHALL WHISTLE COMMUNICATIONS BE LIABLE FOR ANY DAMAGES
 * RESULTING FROM OR ARISING OUT OF ANY USE OF THIS SOFTWARE, INCLUDING
 * WITHOUT LIMITATION, ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * PUNITIVE, OR CONSEQUENTIAL DAMAGES, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES, LOSS OF USE, DATA OR PROFITS, HOWEVER CAUSED AND UNDER ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF WHISTLE COMMUNICATIONS IS ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * Author: Julian Elischer <julian@freebsd.org>
 *
 * $FreeBSD: src/sys/netgraph/ng_socketvar.h,v 1.10 2005/07/05 17:35:20 glebius Exp $
 * $DragonFly: src/sys/netgraph7/ng_socketvar.h,v 1.2 2008/06/26 23:05:35 dillon Exp $
 * $Whistle: ng_socketvar.h,v 1.1 1999/01/20 21:35:39 archie Exp $
 */

#ifndef _NETGRAPH_NG_SOCKETVAR_H_
#define _NETGRAPH_NG_SOCKETVAR_H_

/* Netgraph protocol control block for each socket */
struct ngpcb {
	struct socket	 *ng_socket;	/* the socket */
	struct ngsock	 *sockdata;	/* netgraph info */
	LIST_ENTRY(ngpcb) socks;	/* linked list of sockets */
	int		  type;		/* NG_CONTROL or NG_DATA */
};

/* Per-node private data */
struct ngsock {
	struct ng_node	*node;		/* the associated netgraph node */
	struct ngpcb	*datasock;	/* optional data socket */
	struct ngpcb	*ctlsock;	/* optional control socket */
	int    flags;
	int    refs;
	struct mtx	mtx;		/* mtx to wait on */
	int		error;		/* place to store error */
};
#define	NGS_FLAG_NOLINGER	1	/* close with last hook */

#endif /* _NETGRAPH_NG_SOCKETVAR_H_ */
