/*
 * Sun RPC is a product of Sun Microsystems, Inc. and is provided for
 * unrestricted use provided that this legend is included on all tape
 * media and as a part of the software program in whole or part.  Users
 * may copy or modify Sun RPC without charge, but are not authorized
 * to license or distribute it to anyone else except as part of a product or
 * program developed by the user.
 *
 * SUN RPC IS PROVIDED AS IS WITH NO WARRANTIES OF ANY KIND INCLUDING THE
 * WARRANTIES OF DESIGN, MERCHANTIBILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE, OR ARISING FROM A COURSE OF DEALING, USAGE OR TRADE PRACTICE.
 *
 * Sun RPC is provided with no support and without any obligation on the
 * part of Sun Microsystems, Inc. to assist in its use, correction,
 * modification or enhancement.
 *
 * SUN MICROSYSTEMS, INC. SHALL HAVE NO LIABILITY WITH RESPECT TO THE
 * INFRINGEMENT OF COPYRIGHTS, TRADE SECRETS OR ANY PATENTS BY SUN RPC
 * OR ANY PART THEREOF.
 *
 * In no event will Sun Microsystems, Inc. be liable for any lost revenue
 * or profits or other special, indirect and consequential damages, even if
 * Sun has been advised of the possibility of such damages.
 *
 * Sun Microsystems, Inc.
 * 2550 Garcia Avenue
 * Mountain View, California  94043
 *
 * @(#)xdr_float.c 1.12 87/08/11 Copyr 1984 Sun Micro
 * @(#)xdr_float.c	2.1 88/07/29 4.0 RPCSRC
 * $NetBSD: xdr_float.c,v 1.23 2000/07/17 04:59:51 matt Exp $
 * $FreeBSD: src/lib/libc/xdr/xdr_float.c,v 1.14 2004/10/16 06:32:43 obrien Exp $
 */

/*
 * xdr_float.c, Generic XDR routines implementation.
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 *
 * These are the "floating point" xdr routines used to (de)serialize
 * most common data items.  See xdr.h for more info on the interface to
 * xdr.
 */

#include "namespace.h"
#include <sys/types.h>
#include <sys/param.h>

#include <machine/endian.h>

#include <stdio.h>

#include <rpc/types.h>
#include <rpc/xdr.h>
#include "un-namespace.h"

/*
 * This routine works on machines with IEEE754 FP.
 */

bool_t
xdr_float(XDR *xdrs, float *fp)
{
	switch (xdrs->x_op) {

	case XDR_ENCODE:
		return (XDR_PUTINT32(xdrs, (int32_t *)fp));

	case XDR_DECODE:
		return (XDR_GETINT32(xdrs, (int32_t *)fp));

	case XDR_FREE:
		return (TRUE);
	}
	/* NOTREACHED */
	return (FALSE);
}

bool_t
xdr_double(XDR *xdrs, double *dp)
{
	int32_t *i32p;
	bool_t rv;

	switch (xdrs->x_op) {

	case XDR_ENCODE:
		i32p = (int32_t *)(void *)dp;
#if BYTE_ORDER == BIG_ENDIAN
		rv = XDR_PUTINT32(xdrs, i32p);
		if (!rv)
			return (rv);
		rv = XDR_PUTINT32(xdrs, i32p+1);
#else
		rv = XDR_PUTINT32(xdrs, i32p+1);
		if (!rv)
			return (rv);
		rv = XDR_PUTINT32(xdrs, i32p);
#endif
		return (rv);

	case XDR_DECODE:
		i32p = (int32_t *)(void *)dp;
#if BYTE_ORDER == BIG_ENDIAN
		rv = XDR_GETINT32(xdrs, i32p);
		if (!rv)
			return (rv);
		rv = XDR_GETINT32(xdrs, i32p+1);
#else
		rv = XDR_GETINT32(xdrs, i32p+1);
		if (!rv)
			return (rv);
		rv = XDR_GETINT32(xdrs, i32p);
#endif
		return (rv);

	case XDR_FREE:
		return (TRUE);
	}
	/* NOTREACHED */
	return (FALSE);
}
