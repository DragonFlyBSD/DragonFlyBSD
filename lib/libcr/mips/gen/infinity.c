/* infinity.c */

/*
 * $OpenBSD: infinity.c,v 1.2 1996/08/19 08:16:01 tholo Exp $
 */
#include <math.h>
#include <sys/types.h>

/* bytes for +Infinity on a MIPS */
#if BYTE_ORDER == BIG_ENDIAN
char __infinity[] = { 0x7f, 0xf0, 0, 0, 0, 0, 0, 0 };
#else
char __infinity[] = { 0, 0, 0, 0, 0, 0, 0xf0, 0x7f };
#endif
