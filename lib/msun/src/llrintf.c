/* $NetBSD: llrintf.c,v 1.1 2004/06/30 15:08:30 drochner Exp $ */
/* $DragonFly: src/lib/msun/src/Attic/llrintf.c,v 1.1 2004/12/30 16:10:18 asmodai Exp $ */

/*
 * Written by Matthias Drochner <drochner@NetBSD.org>.
 * Public domain.
 */

#define LRINTNAME llrintf
#define RESTYPE long long int
#define RESTYPE_MIN LLONG_MIN
#define RESTYPE_MAX LLONG_MAX

#include "lrintf.c"
