/*
 * Copyright (c) 2020 by Solar Designer
 * See LICENSE
 */

#ifndef PASSWDQC_FILTER_H__
#define PASSWDQC_FILTER_H__

#include <stdint.h>

/* Higher-level API for use by passwdqc_check.c */

typedef struct {
	uint8_t version[4]; /* PASSWDQC_FILTER_VERSION */
	uint8_t threshold; /* 0 to 4 */
	uint8_t bucket_size; /* 2 to 4 */
	uint8_t hash_id; /* one of PASSWDQC_FILTER_HASH_* */
	uint8_t reserved1;
	uint64_t endianness; /* PASSWDQC_FILTER_ENDIANNESS */
	uint64_t reserved2; /* e.g., for checksum */
	uint64_t capacity, deletes, inserts, dupes, kicks;
} passwdqc_filter_header_t;

typedef struct {
	passwdqc_filter_header_t header;
	int fd;
} passwdqc_filter_t;

extern int passwdqc_filter_open(passwdqc_filter_t *flt, const char *filename);
extern int passwdqc_filter_lookup(const passwdqc_filter_t *flt, const char *plaintext);
extern int passwdqc_filter_close(passwdqc_filter_t *flt);

#ifdef PASSWDQC_FILTER_INTERNALS
/* Lower-level inlines for shared use by pwqfilter.c and passwdqc_filter.c */

#include <string.h> /* for strcspn() */
#ifndef _MSC_VER
#include <endian.h>
#endif

#include "md4.h"

#define PASSWDQC_FILTER_VERSION "PWQ0"
#define PASSWDQC_FILTER_ENDIANNESS 0x0807060504030201ULL

static inline int passwdqc_filter_verify_header(const passwdqc_filter_header_t *header)
{
	return (memcmp(header->version, PASSWDQC_FILTER_VERSION, sizeof(header->version)) ||
	    header->threshold > header->bucket_size || header->bucket_size < 2 || header->bucket_size > 4 ||
	    header->endianness != PASSWDQC_FILTER_ENDIANNESS ||
	    (header->capacity & 3) || header->capacity < 4 || header->capacity > ((1ULL << 32) - 1) * 4 ||
	    header->inserts - header->deletes > header->capacity) ? -1 : 0;
}

typedef enum {
	PASSWDQC_FILTER_HASH_OPAQUE = 0,
	PASSWDQC_FILTER_HASH_MIN = 1,
	PASSWDQC_FILTER_HASH_MD4 = 1,
	PASSWDQC_FILTER_HASH_NTLM_CP1252 = 2,
	PASSWDQC_FILTER_HASH_MAX = 2
} passwdqc_filter_hash_id_t;

typedef struct {
	uint64_t hi, lo; /* we access hi first, so let's also place it first */
} passwdqc_filter_packed_t;

typedef uint32_t passwdqc_filter_i_t;
typedef uint64_t passwdqc_filter_f_t;

typedef struct {
	passwdqc_filter_f_t slots[4];
} passwdqc_filter_unpacked_t;

typedef union {
	unsigned char uc[16];
	uint32_t u32[4];
	uint64_t u64[2];
} passwdqc_filter_hash_t;

#ifdef __GNUC__
#define force_inline	__attribute__ ((always_inline)) inline
#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)
#else
#define force_inline	inline
#define likely(x)	(x)
#define unlikely(x)	(x)
#endif

static force_inline passwdqc_filter_i_t passwdqc_filter_wrap(uint32_t what, passwdqc_filter_i_t m)
{
	return ((uint64_t)what * m) >> 32;
}

static force_inline passwdqc_filter_i_t passwdqc_filter_h2i(passwdqc_filter_hash_t *h, passwdqc_filter_i_t m)
{
	uint32_t i;
/*
 * Controversial optimization: when converting a hash to its hash table index
 * for the primary bucket, take its initial portion and swap the nibbles so
 * that we process most of the hash table semi-sequentially in case our input
 * is an ASCII-sorted list of hex-encoded hashes.  A drawback is that we fail
 * to reach high load if our input is a biased fragment from such sorted list.
 */
#if defined(__BYTE_ORDER) && __BYTE_ORDER == __BIG_ENDIAN
	i = h->u32[0];
#else
	i = (uint32_t)h->uc[0] << 24;
	i |= (uint32_t)h->uc[1] << 16;
	i |= (uint32_t)h->uc[2] << 8;
	i |= (uint32_t)h->uc[3];
#endif
	i = ((i & 0x0f0f0f0f) << 4) | ((i >> 4) & 0x0f0f0f0f);
	return passwdqc_filter_wrap(i, m);
}

static force_inline passwdqc_filter_f_t passwdqc_filter_h2f(passwdqc_filter_hash_t *h)
{
#if defined(__BYTE_ORDER) && __BYTE_ORDER == __LITTLE_ENDIAN
	return h->u64[1];
#else
	uint64_t f;
	f = (uint64_t)h->uc[8];
	f |= (uint64_t)h->uc[9] << 8;
	f |= (uint64_t)h->uc[10] << 16;
	f |= (uint64_t)h->uc[11] << 24;
	f |= (uint64_t)h->uc[12] << 32;
	f |= (uint64_t)h->uc[13] << 40;
	f |= (uint64_t)h->uc[14] << 48;
	f |= (uint64_t)h->uc[15] << 56;
	return f;
#endif
}

static force_inline passwdqc_filter_i_t passwdqc_filter_alti(passwdqc_filter_i_t i, passwdqc_filter_f_t f, passwdqc_filter_i_t m)
{
/*
 * We must not use more than 33 bits of the fingerprint here for consistent
 * behavior in case the fingerprint later gets truncated.
 */
	int64_t alti = (int64_t)(m - 1 - i) - passwdqc_filter_wrap((uint32_t)f, m);
#if 0
/*
 * This is how we could have made use of the 33rd bit while staying in range,
 * but testing shows that this is unnecessary.
 */
	alti -= ((f >> 32) & 1);
#endif
	if (alti < 0)
		alti += m;
#if 0
	assert((passwdqc_filter_i_t)alti < m);
#endif
	return (passwdqc_filter_i_t)alti;
}

static inline unsigned int passwdqc_filter_ssdecode(unsigned int src)
{
	/* First 16 tetrahedral numbers (n*(n+1)*(n+2)/3!) in reverse order */
	static const uint16_t tab4[] = {816, 680, 560, 455, 364, 286, 220, 165, 120, 84, 56, 35, 20, 10, 4, 1};
	/* First 16 triangular numbers (n*(n+1)/2!) in reverse order */
	static const uint8_t tab3[] = {136, 120, 105, 91, 78, 66, 55, 45, 36, 28, 21, 15, 10, 6, 3, 1};

	unsigned int dst, i = 0;

	while (src >= tab4[i])
		src -= tab4[i++];
	dst = i << 12;

	while (src >= tab3[i])
		src -= tab3[i++];
	dst |= i << 8;

	while (src >= 16 - i)
		src -= 16 - i++;
	dst |= i << 4;

	dst |= i + src;

	return dst;
}

static force_inline int passwdqc_filter_unpack(passwdqc_filter_unpacked_t *dst, const passwdqc_filter_packed_t *src,
    const uint16_t *ssdecode)
{
	uint64_t hi = src->hi, f = src->lo;
	unsigned int ssi = hi >> (64 - 12); /* semi-sort index */

	if (likely(ssi - 1 < 3876)) {
		passwdqc_filter_f_t ssd = ssdecode ? ssdecode[ssi - 1] : passwdqc_filter_ssdecode(ssi - 1);
		const unsigned int fbits = 33;
		const unsigned int lobits = fbits - 4;
		const passwdqc_filter_f_t lomask = ((passwdqc_filter_f_t)1 << lobits) - 1;
		dst->slots[0] = (f & lomask) | ((ssd & 0x000f) << lobits);
		f >>= lobits;
		dst->slots[1] = (f & lomask) | ((ssd & 0x00f0) << (lobits - 4));
		f >>= lobits;
		f |= hi << (64 - 2 * lobits);
		dst->slots[2] = (f & lomask) | ((ssd & 0x0f00) << (lobits - 8));
		f >>= lobits;
		dst->slots[3] = (f & lomask) | ((ssd & 0xf000) << (lobits - 12));
		return 4;
	}

	if (likely(hi <= 1)) {
		if (!hi)
			return unlikely(f) ? -1 : 0;

		dst->slots[0] = f;
		return 1;
	}

	if (likely((ssi & 0xf80) == 0xf80)) {
		const unsigned int fbits = 41;
		const passwdqc_filter_f_t fmask = ((passwdqc_filter_f_t)1 << fbits) - 1;
		dst->slots[0] = f & fmask;
		f >>= fbits;
		f |= hi << (64 - fbits);
		dst->slots[1] = f & fmask;
		if (unlikely(dst->slots[0] < dst->slots[1]))
			return -1;
		f = hi >> (2 * fbits - 64);
		dst->slots[2] = f & fmask;
		if (unlikely(dst->slots[1] < dst->slots[2]))
			return -1;
		return 3;
	}

	if (likely((ssi & 0xfc0) == 0xf40)) {
		const unsigned int fbits = 61;
		const passwdqc_filter_f_t fmask = ((passwdqc_filter_f_t)1 << fbits) - 1;
		dst->slots[0] = f & fmask;
		f >>= fbits;
		f |= hi << (64 - fbits);
		dst->slots[1] = f & fmask;
		if (unlikely(dst->slots[0] < dst->slots[1]))
			return -1;
		return 2;
	}

	return -1;
}

static inline int passwdqc_filter_f_eq(passwdqc_filter_f_t stored, passwdqc_filter_f_t full, unsigned int largest_bucket_size)
{
	if (likely((uint32_t)stored != (uint32_t)full))
		return 0;
/*
 * Ignore optional high bits of a stored fingerprint if they're all-zero,
 * regardless of whether the fingerprint possibly came from a large enough slot
 * for those zeroes to potentially be meaningful.  We have to do this because
 * the fingerprint might have been previously stored in a larger (smaller-slot)
 * bucket and been kicked from there, in which case the zeroes are meaningless.
 * Exception: we don't have to do this if there were no larger buckets so far.
 */
	if ((stored >> 33) || largest_bucket_size < 4) {
		if ((stored >> 41) || largest_bucket_size < 3) {
			if (stored >> 61)
				return likely(stored == full);
			else
				return likely(stored == (full & (((passwdqc_filter_f_t)1 << 61) - 1)));
		} else {
			return likely(stored == (full & (((passwdqc_filter_f_t)1 << 41) - 1)));
		}
	} else {
		return likely(stored == (full & (((passwdqc_filter_f_t)1 << 33) - 1)));
	}
}

static inline void passwdqc_filter_md4(passwdqc_filter_hash_t *dst, const char *src)
{
	MD4_CTX ctx;
	MD4_Init(&ctx);
	MD4_Update(&ctx, src, strcspn(src, "\n\r"));
	MD4_Final(dst->uc, &ctx);
}

static inline void passwdqc_filter_ntlm_cp1252(passwdqc_filter_hash_t *dst, const char *src)
{
/*
 * 5 of these codes are undefined in CP1252.  We let the original single-byte
 * values for them pass through, which appears to match how the HIBP v7 NTLM
 * hashes were generated.
 */
	static const uint16_t c1[] = {
		0x20ac, 0x81, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
		0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8d, 0x017d, 0x8f,
		0x90, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
		0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x9d, 0x017e, 0x0178
	};

	MD4_CTX ctx;
	MD4_Init(&ctx);
	while (*src != '\n' && *src != '\r' && *src) {
		unsigned int c = *(unsigned char *)src++;
		if (c - 128 < sizeof(c1) / sizeof(c1[0]))
			c = c1[c - 128];
#if defined(__BYTE_ORDER) && __BYTE_ORDER == __LITTLE_ENDIAN
		MD4_Update(&ctx, &c, 2);
#else
		uint8_t ucs2[2] = {c, c >> 8};
		MD4_Update(&ctx, ucs2, 2);
#endif
	}
	MD4_Final(dst->uc, &ctx);
}

#endif /* PASSWDQC_FILTER_INTERNALS */
#endif /* PASSWDQC_FILTER_H__ */
