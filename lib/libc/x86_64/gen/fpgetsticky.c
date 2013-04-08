/* $FreeBSD: head/lib/libc/amd64/gen/fpgetsticky.c 117864 2003-07-22 06:46:17Z peter $ */
#define __IEEEFP_NOINLINES__ 1
#include <ieeefp.h>

fp_except_t fpgetsticky(void)
{
	return __fpgetsticky();
}
