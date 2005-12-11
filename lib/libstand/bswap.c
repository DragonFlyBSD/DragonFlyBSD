/*
 * Written by Manuel Bouyer <bouyer@netbsd.org>.
 * Public domain.
 *
 * $NetBSD: bswap32.c,v 1.1 1997/10/09 15:42:33 bouyer Exp $
 * $NetBSD: bswap64.c,v 1.1 1997/10/09 15:42:33 bouyer Exp $
 * $DragonFly: src/lib/libstand/bswap.c,v 1.3 2005/12/11 02:27:26 swildner Exp $
 */

#include <sys/types.h>

#undef bswap32
#undef bswap64

u_int32_t
bswap32(u_int32_t x)
{
	return  ((x << 24) & 0xff000000 ) |
			((x <<  8) & 0x00ff0000 ) |
			((x >>  8) & 0x0000ff00 ) |
			((x >> 24) & 0x000000ff );
}

u_int64_t
bswap64(u_int64_t x)
{  
	u_int32_t *p = (u_int32_t*)&x;
	u_int32_t t;
	t = bswap32(p[0]);
	p[0] = bswap32(p[1]);
	p[1] = t;
	return x;
}   
