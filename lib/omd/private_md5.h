/*
 * MD5C.C - RSA Data Security, Inc., MD5 message-digest algorithm
 *
 * Copyright (C) 1991-2, RSA Data Security, Inc. Created 1991. All
 * rights reserved.
 *
 * License to copy and use this software is granted provided that it
 * is identified as the "RSA Data Security, Inc. MD5 Message-Digest
 * Algorithm" in all material mentioning or referencing this software
 * or this function.
 *
 * License is also granted to make and use derivative works provided
 * that such works are identified as "derived from the RSA Data
 * Security, Inc. MD5 Message-Digest Algorithm" in all material
 * mentioning or referencing the derived work.
 *
 * RSA Data Security, Inc. makes no representations concerning either
 * the merchantability of this software or the suitability of this
 * software for any particular purpose. It is provided "as is"
 * without express or implied warranty of any kind.
 *
 * These notices must be retained in any copies of any part of this
 * documentation and/or software.
 *
 * This code is the same as the code published by RSA Inc.  It has been
 * edited for clarity, style and inlineability.
 */

/*
 * This header shall not have include guards, be included only locally
 * and not export/pass MD5_CTX unless WITH_OPENSSL.
 */

#include <sys/cdefs.h>
#include <sys/types.h>
#include <string.h>
#include <machine/endian.h>
#include <sys/endian.h>

#ifdef WITH_OPENSSL
#include <openssl/md5.h>
#else
/* XXX must match <openssl/md5.h> !!! */
#define	MD5_CBLOCK		64
#define	MD5_DIGEST_LENGTH	16
#define MD5_LBLOCK		(MD5_CBLOCK/4)
#define	MD5_LONG		unsigned int
typedef struct MD5state_st {
	MD5_LONG A,B,C,D;
	MD5_LONG Nl,Nh;
	MD5_LONG data[MD5_LBLOCK];
	unsigned int num;
} MD5_CTX;
#endif

#define MD5_BLOCK_LENGTH		MD5_CBLOCK
#define MD5_DIGEST_STRING_LENGTH	(MD5_DIGEST_LENGTH * 2 + 1)

#if (BYTE_ORDER == LITTLE_ENDIAN)
#define _md5_Encode memcpy
#define _md5_Decode memcpy
#else

/*
 * Encodes input (u_int32_t) into output (unsigned char). Assumes len is
 * a multiple of 4.
 */
static void
_md5_Encode (unsigned char *output, u_int32_t *input, unsigned int len)
{
	unsigned int i;
	u_int32_t *op = (u_int32_t *)output;

	for (i = 0; i < len / 4; i++)
		op[i] = htole32(input[i]);
}

/*
 * Decodes input (unsigned char) into output (u_int32_t). Assumes len is
 * a multiple of 4.
 */
static void
_md5_Decode (u_int32_t *output, const unsigned char *input, unsigned int len)
{
	unsigned int i;
	const u_int32_t *ip = (const u_int32_t *)input;

	for (i = 0; i < len / 4; i++)
		output[i] = le32toh(ip[i]);
}
#endif

static unsigned char _md5_PADDING[64] = {
  0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* F, G, H and I are basic MD5 functions. */
#define _md5_F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define _md5_G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define _md5_H(x, y, z) ((x) ^ (y) ^ (z))
#define _md5_I(x, y, z) ((y) ^ ((x) | (~z)))

/* ROTATE_LEFT rotates x left n bits. */
#define _md5_ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32-(n))))

/*
 * FF, GG, HH, and II transformations for rounds 1, 2, 3, and 4.
 * Rotation is separate from addition to prevent recomputation.
 */
#define _md5_FF(a, b, c, d, x, s, ac) { \
	(a) += _md5_F ((b), (c), (d)) + (x) + (u_int32_t)(ac); \
	(a) = _md5_ROTATE_LEFT ((a), (s)); \
	(a) += (b); \
	}
#define _md5_GG(a, b, c, d, x, s, ac) { \
	(a) += _md5_G ((b), (c), (d)) + (x) + (u_int32_t)(ac); \
	(a) = _md5_ROTATE_LEFT ((a), (s)); \
	(a) += (b); \
	}
#define _md5_HH(a, b, c, d, x, s, ac) { \
	(a) += _md5_H ((b), (c), (d)) + (x) + (u_int32_t)(ac); \
	(a) = _md5_ROTATE_LEFT ((a), (s)); \
	(a) += (b); \
	}
#define _md5_II(a, b, c, d, x, s, ac) { \
	(a) += _md5_I ((b), (c), (d)) + (x) + (u_int32_t)(ac); \
	(a) = _md5_ROTATE_LEFT ((a), (s)); \
	(a) += (b); \
	}

/* MD5 basic transformation. Transforms state based on block. */
static void
MD5Transform (u_int32_t *state, const unsigned char *block)
{
	u_int32_t a = state[0], b = state[1], c = state[2], d = state[3], x[16];

	_md5_Decode (x, block, 64);

	/* Round 1 */
#define _md5_S11 7
#define _md5_S12 12
#define _md5_S13 17
#define _md5_S14 22
	_md5_FF (a, b, c, d, x[ 0], _md5_S11, 0xd76aa478); /* 1 */
	_md5_FF (d, a, b, c, x[ 1], _md5_S12, 0xe8c7b756); /* 2 */
	_md5_FF (c, d, a, b, x[ 2], _md5_S13, 0x242070db); /* 3 */
	_md5_FF (b, c, d, a, x[ 3], _md5_S14, 0xc1bdceee); /* 4 */
	_md5_FF (a, b, c, d, x[ 4], _md5_S11, 0xf57c0faf); /* 5 */
	_md5_FF (d, a, b, c, x[ 5], _md5_S12, 0x4787c62a); /* 6 */
	_md5_FF (c, d, a, b, x[ 6], _md5_S13, 0xa8304613); /* 7 */
	_md5_FF (b, c, d, a, x[ 7], _md5_S14, 0xfd469501); /* 8 */
	_md5_FF (a, b, c, d, x[ 8], _md5_S11, 0x698098d8); /* 9 */
	_md5_FF (d, a, b, c, x[ 9], _md5_S12, 0x8b44f7af); /* 10 */
	_md5_FF (c, d, a, b, x[10], _md5_S13, 0xffff5bb1); /* 11 */
	_md5_FF (b, c, d, a, x[11], _md5_S14, 0x895cd7be); /* 12 */
	_md5_FF (a, b, c, d, x[12], _md5_S11, 0x6b901122); /* 13 */
	_md5_FF (d, a, b, c, x[13], _md5_S12, 0xfd987193); /* 14 */
	_md5_FF (c, d, a, b, x[14], _md5_S13, 0xa679438e); /* 15 */
	_md5_FF (b, c, d, a, x[15], _md5_S14, 0x49b40821); /* 16 */

	/* Round 2 */
#define _md5_S21 5
#define _md5_S22 9
#define _md5_S23 14
#define _md5_S24 20
	_md5_GG (a, b, c, d, x[ 1], _md5_S21, 0xf61e2562); /* 17 */
	_md5_GG (d, a, b, c, x[ 6], _md5_S22, 0xc040b340); /* 18 */
	_md5_GG (c, d, a, b, x[11], _md5_S23, 0x265e5a51); /* 19 */
	_md5_GG (b, c, d, a, x[ 0], _md5_S24, 0xe9b6c7aa); /* 20 */
	_md5_GG (a, b, c, d, x[ 5], _md5_S21, 0xd62f105d); /* 21 */
	_md5_GG (d, a, b, c, x[10], _md5_S22,  0x2441453); /* 22 */
	_md5_GG (c, d, a, b, x[15], _md5_S23, 0xd8a1e681); /* 23 */
	_md5_GG (b, c, d, a, x[ 4], _md5_S24, 0xe7d3fbc8); /* 24 */
	_md5_GG (a, b, c, d, x[ 9], _md5_S21, 0x21e1cde6); /* 25 */
	_md5_GG (d, a, b, c, x[14], _md5_S22, 0xc33707d6); /* 26 */
	_md5_GG (c, d, a, b, x[ 3], _md5_S23, 0xf4d50d87); /* 27 */
	_md5_GG (b, c, d, a, x[ 8], _md5_S24, 0x455a14ed); /* 28 */
	_md5_GG (a, b, c, d, x[13], _md5_S21, 0xa9e3e905); /* 29 */
	_md5_GG (d, a, b, c, x[ 2], _md5_S22, 0xfcefa3f8); /* 30 */
	_md5_GG (c, d, a, b, x[ 7], _md5_S23, 0x676f02d9); /* 31 */
	_md5_GG (b, c, d, a, x[12], _md5_S24, 0x8d2a4c8a); /* 32 */

	/* Round 3 */
#define _md5_S31 4
#define _md5_S32 11
#define _md5_S33 16
#define _md5_S34 23
	_md5_HH (a, b, c, d, x[ 5], _md5_S31, 0xfffa3942); /* 33 */
	_md5_HH (d, a, b, c, x[ 8], _md5_S32, 0x8771f681); /* 34 */
	_md5_HH (c, d, a, b, x[11], _md5_S33, 0x6d9d6122); /* 35 */
	_md5_HH (b, c, d, a, x[14], _md5_S34, 0xfde5380c); /* 36 */
	_md5_HH (a, b, c, d, x[ 1], _md5_S31, 0xa4beea44); /* 37 */
	_md5_HH (d, a, b, c, x[ 4], _md5_S32, 0x4bdecfa9); /* 38 */
	_md5_HH (c, d, a, b, x[ 7], _md5_S33, 0xf6bb4b60); /* 39 */
	_md5_HH (b, c, d, a, x[10], _md5_S34, 0xbebfbc70); /* 40 */
	_md5_HH (a, b, c, d, x[13], _md5_S31, 0x289b7ec6); /* 41 */
	_md5_HH (d, a, b, c, x[ 0], _md5_S32, 0xeaa127fa); /* 42 */
	_md5_HH (c, d, a, b, x[ 3], _md5_S33, 0xd4ef3085); /* 43 */
	_md5_HH (b, c, d, a, x[ 6], _md5_S34,  0x4881d05); /* 44 */
	_md5_HH (a, b, c, d, x[ 9], _md5_S31, 0xd9d4d039); /* 45 */
	_md5_HH (d, a, b, c, x[12], _md5_S32, 0xe6db99e5); /* 46 */
	_md5_HH (c, d, a, b, x[15], _md5_S33, 0x1fa27cf8); /* 47 */
	_md5_HH (b, c, d, a, x[ 2], _md5_S34, 0xc4ac5665); /* 48 */

	/* Round 4 */
#define _md5_S41 6
#define _md5_S42 10
#define _md5_S43 15
#define _md5_S44 21
	_md5_II (a, b, c, d, x[ 0], _md5_S41, 0xf4292244); /* 49 */
	_md5_II (d, a, b, c, x[ 7], _md5_S42, 0x432aff97); /* 50 */
	_md5_II (c, d, a, b, x[14], _md5_S43, 0xab9423a7); /* 51 */
	_md5_II (b, c, d, a, x[ 5], _md5_S44, 0xfc93a039); /* 52 */
	_md5_II (a, b, c, d, x[12], _md5_S41, 0x655b59c3); /* 53 */
	_md5_II (d, a, b, c, x[ 3], _md5_S42, 0x8f0ccc92); /* 54 */
	_md5_II (c, d, a, b, x[10], _md5_S43, 0xffeff47d); /* 55 */
	_md5_II (b, c, d, a, x[ 1], _md5_S44, 0x85845dd1); /* 56 */
	_md5_II (a, b, c, d, x[ 8], _md5_S41, 0x6fa87e4f); /* 57 */
	_md5_II (d, a, b, c, x[15], _md5_S42, 0xfe2ce6e0); /* 58 */
	_md5_II (c, d, a, b, x[ 6], _md5_S43, 0xa3014314); /* 59 */
	_md5_II (b, c, d, a, x[13], _md5_S44, 0x4e0811a1); /* 60 */
	_md5_II (a, b, c, d, x[ 4], _md5_S41, 0xf7537e82); /* 61 */
	_md5_II (d, a, b, c, x[11], _md5_S42, 0xbd3af235); /* 62 */
	_md5_II (c, d, a, b, x[ 2], _md5_S43, 0x2ad7d2bb); /* 63 */
	_md5_II (b, c, d, a, x[ 9], _md5_S44, 0xeb86d391); /* 64 */

	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;

	/* Zeroize sensitive information. */
	memset ((void *)x, 0, sizeof (x));
}

/* MD5 initialization. Begins an MD5 operation, writing a new context. */
static int
MD5Init (MD5_CTX *context)
{

	context->Nl = context->Nh = 0;

	/* Load magic initialization constants.  */
	context->A = 0x67452301;
	context->B = 0xefcdab89;
	context->C = 0x98badcfe;
	context->D = 0x10325476;
	return 1;
}
/*
 * MD5 block update operation. Continues an MD5 message-digest
 * operation, processing another message block, and updating the
 * context.
 */
static void
MD5Update (MD5_CTX *context, const void *in, unsigned int inputLen)
{
	unsigned int i, idx, partLen;
	const unsigned char *input = in;

	/* Compute number of bytes mod 64 */
	idx = (unsigned int)((context->Nl >> 3) & 0x3F);

	/* Update number of bits */
	if ((context->Nl += ((u_int32_t)inputLen << 3))
	    < ((u_int32_t)inputLen << 3))
		context->Nh++;
	context->Nh += ((u_int32_t)inputLen >> 29);

	partLen = 64 - idx;

	/* Transform as many times as possible. */
	if (inputLen >= partLen) {
		memcpy(&((char *)context->data)[idx], (const void *)input,
		    partLen);
		MD5Transform (&context->A, (unsigned char *)context->data);

		for (i = partLen; i + 63 < inputLen; i += 64)
			MD5Transform (&context->A, &input[i]);

		idx = 0;
	}
	else
		i = 0;

	/* Buffer remaining input */
	memcpy (&((char *)context->data)[idx], (const void *)&input[i],
	    inputLen-i);
}

/*
 * MD5 padding. Adds padding followed by original length.
 */
static void
MD5Pad (MD5_CTX *context)
{
	unsigned char bits[8];
	unsigned int idx, padLen;

	/* Save number of bits */
	_md5_Encode (bits, &context->Nl, 8);

	/* Pad out to 56 mod 64. */
	idx = (unsigned int)((context->Nl >> 3) & 0x3f);
	padLen = (idx < 56) ? (56 - idx) : (120 - idx);
	MD5Update (context, _md5_PADDING, padLen);

	/* Append length (before padding) */
	MD5Update (context, bits, 8);
}

/*
 * MD5 finalization. Ends an MD5 message-digest operation, writing the
 * the message digest and zeroizing the context.
 */
static void
MD5Final (unsigned char digest[16], MD5_CTX *context)
{
	/* Do padding. */
	MD5Pad (context);

	/* Store state in digest */
	_md5_Encode (digest, &context->A, 16);

	/* Zeroize sensitive information. */
	memset ((void *)context, 0, sizeof (*context));
}
