/*
 * $FreeBSD: src/lib/libc/gen/getprogname.c,v 1.4 2002/03/29 22:43:41 markm Exp $
 * $DragonFly: src/lib/libc/gen/getprogname.c,v 1.3 2005/03/09 18:52:21 joerg Exp $
 */

#include "namespace.h"
#include <stdlib.h>
#include "un-namespace.h"

#include "libc_private.h"

__weak_reference(_getprogname, getprogname);

const char *
_getprogname(void)
{

	return (__progname);
}
