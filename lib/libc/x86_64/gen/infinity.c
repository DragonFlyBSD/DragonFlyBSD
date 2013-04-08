/*
 * infinity.c
 * $FreeBSD: head/lib/libc/amd64/gen/infinity.c 110566 2003-02-08 20:37:55Z mike $
 */

#include <math.h>

/* bytes for +Infinity on a 387 */
const union __infinity_un __infinity = { { 0, 0, 0, 0, 0, 0, 0xf0, 0x7f } };

/* bytes for NaN */
const union __nan_un __nan = { { 0, 0, 0xc0, 0xff } };
