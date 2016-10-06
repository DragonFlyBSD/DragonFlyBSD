/*
 * $FreeBSD: src/lib/libc/gen/getprogname.c,v 1.4 2002/03/29 22:43:41 markm Exp $
 */

#include "namespace.h"
#include <stdlib.h>
#include "un-namespace.h"

#include "libc_private.h"

const char *
_getprogname(void)
{

	return (__progname);
}

__weak_reference(_getprogname, getprogname);
