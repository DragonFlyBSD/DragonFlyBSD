/*
 * Mach Operating System
 * Copyright (c) 1991,1990 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 *
 * $FreeBSD: src/sys/ddb/db_access.c,v 1.15 1999/08/28 00:41:04 peter Exp $
 */

/*
 *	Author: David B. Golub, Carnegie Mellon University
 *	Date:	7/90
 */
#include <sys/param.h>
#include <machine/limits.h>

#include <ddb/ddb.h>
#include <ddb/db_access.h>

/*
 * Access unaligned data items on aligned (longword)
 * boundaries.
 */

#if LONG_BIT == 32

static unsigned db_extend[] = {	/* table for sign-extending */
	0,
	0xFFFFFF80U,
	0xFFFF8000U,
	0xFF800000U
};

#else

static unsigned long db_extend[] = {	/* table for sign-extending */
	0,
	0xFFFFFFFFFFFFFF80LU,
	0xFFFFFFFFFFFF8000LU,
	0xFFFFFFFFFF800000LU,
	0xFFFFFFFF80000000LU,
	0xFFFFFF8000000000LU,
	0xFFFF800000000000LU,
	0xFF80000000000000LU
};

#endif

#ifndef BYTE_MSF
#define	BYTE_MSF	0
#endif

db_expr_t
db_get_value(db_addr_t addr, int size, boolean_t is_signed)
{
	char	data[sizeof(long)];
	int	i;
	db_expr_t value;

	if (size > sizeof(long))
		size = sizeof(long);

	db_read_bytes(addr, size, data);

	value = 0;
#if	BYTE_MSF
	for (i = 0; i < size; i++)
#else	/* BYTE_LSF */
	for (i = size - 1; i >= 0; i--)
#endif
	{
		value = (value << 8) + (data[i] & 0xFF);
	}

	if (size < sizeof(long)) {
		if (is_signed && (value & db_extend[size]) != 0)
			value |= db_extend[size];
	}
	return (value);
}

void
db_put_value(db_addr_t addr, int size, db_expr_t value)
{
	char	data[sizeof(long)];
	int	i;

	if (size > sizeof(long))
		size = sizeof(long);

#if	BYTE_MSF
	for (i = size - 1; i >= 0; i--)
#else	/* BYTE_LSF */
	for (i = 0; i < size; i++)
#endif
	{
		data[i] = value & 0xFF;
		value >>= 8;
	}

	db_write_bytes(addr, size, data);
}
