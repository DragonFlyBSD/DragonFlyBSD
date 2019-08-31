/*
 * Fowler / Noll / Vo Hash (FNV Hash)
 * http://www.isthe.com/chongo/tech/comp/fnv/
 *
 * This is an implementation of the algorithms posted above.
 * This file is placed in the public domain by Peter Wemm.
 *
 * $FreeBSD: src/sys/sys/fnv_hash.h,v 1.2.2.1 2001/03/21 10:50:59 peter Exp $
 */

#ifndef _SYS_FNV_HASH_H_
#define _SYS_FNV_HASH_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif

typedef u_int32_t Fnv32_t;

#define FNV1_32_INIT ((Fnv32_t) 33554467UL)
#define FNV_32_PRIME ((Fnv32_t) 0x01000193UL)

static __inline Fnv32_t
fnv_32_buf(const void *buf, size_t len, Fnv32_t hval)
{
	const u_int8_t *s = (const u_int8_t *)buf;

	while (len-- != 0) {
		hval *= FNV_32_PRIME;
		hval ^= *s++;
	}
	return hval;
}

/* currently unused */
#if 0
static __inline Fnv32_t
fnv_32_str(const char *str, Fnv32_t hval)
{
	const u_int8_t *s = (const u_int8_t *)str;
	Fnv32_t c;

	while ((c = *s++) != 0) {
		hval *= FNV_32_PRIME;
		hval ^= c;
	}
	return hval;
}
#endif

#endif	/* !_SYS_FNV_HASH_H_ */
