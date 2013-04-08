/* $FreeBSD: head/lib/msun/amd64/s_llrint.S 218305 2011-02-04 21:54:06Z kib $ */
#include <machine/asm.h>

/* sizeof(long) == sizeof(long long) */
#define	fn	llrint
#include "s_lrint.S"
