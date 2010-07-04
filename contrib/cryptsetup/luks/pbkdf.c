/* Implementation of Password-Based Cryptography as per PKCS#5
 * Copyright (C) 2002,2003 Simon Josefsson
 * Copyright (C) 2004 Free Software Foundation
 *
 * LUKS code
 * Copyright (C) 2004 Clemens Fruhwirth <clemens@endorphin.org>
 * Copyright (C) 2009 Red Hat, Inc. All rights reserved.
 *
 * This file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <netinet/in.h>
#include <errno.h>
#include <signal.h>
#include <alloca.h>
#include <sys/time.h>
#include <gcrypt.h>

static volatile uint64_t __PBKDF2_global_j = 0;
static volatile uint64_t __PBKDF2_performance = 0;

int init_crypto(void);

/*
 * 5.2 PBKDF2
 *
 *  PBKDF2 applies a pseudorandom function (see Appendix B.1 for an
 *  example) to derive keys. The length of the derived key is essentially
 *  unbounded. (However, the maximum effective search space for the
 *  derived key may be limited by the structure of the underlying
 *  pseudorandom function. See Appendix B.1 for further discussion.)
 *  PBKDF2 is recommended for new applications.
 *
 *  PBKDF2 (P, S, c, dkLen)
 *
 *  Options:        PRF        underlying pseudorandom function (hLen
 *                             denotes the length in octets of the
 *                             pseudorandom function output)
 *
 *  Input:          P          password, an octet string (ASCII or UTF-8)
 *                  S          salt, an octet string
 *                  c          iteration count, a positive integer
 *                  dkLen      intended length in octets of the derived
 *                             key, a positive integer, at most
 *                             (2^32 - 1) * hLen
 *
 *  Output:         DK         derived key, a dkLen-octet string
 */

#define MAX_PRF_BLOCK_LEN 80

static int pkcs5_pbkdf2(const char *hash,
			const char *P, size_t Plen,
			const char *S, size_t Slen,
			unsigned int c, unsigned int dkLen,
			char *DK, int perfcheck)
{
	gcry_md_hd_t prf;
	char U[MAX_PRF_BLOCK_LEN];
	char T[MAX_PRF_BLOCK_LEN];
	int PRF, i, k, rc = -EINVAL;
	unsigned int u, hLen, l, r;
	unsigned char *p;
	size_t tmplen = Slen + 4;
	char *tmp;

	tmp = alloca(tmplen);
	if (tmp == NULL)
		return -ENOMEM;

	if (init_crypto())
		return -ENOSYS;

	PRF = gcry_md_map_name(hash);
	if (PRF == 0)
		return -EINVAL;

	hLen = gcry_md_get_algo_dlen(PRF);
	if (hLen == 0 || hLen > MAX_PRF_BLOCK_LEN)
		return -EINVAL;

	if (c == 0)
		return -EINVAL;

	if (dkLen == 0)
		return -EINVAL;

	/*
	 *
	 *  Steps:
	 *
	 *     1. If dkLen > (2^32 - 1) * hLen, output "derived key too long" and
	 *        stop.
	 */

	if (dkLen > 4294967295U)
		return -EINVAL;

	/*
	 *     2. Let l be the number of hLen-octet blocks in the derived key,
	 *        rounding up, and let r be the number of octets in the last
	 *        block:
	 *
	 *                  l = CEIL (dkLen / hLen) ,
	 *                  r = dkLen - (l - 1) * hLen .
	 *
	 *        Here, CEIL (x) is the "ceiling" function, i.e. the smallest
	 *        integer greater than, or equal to, x.
	 */

	l = dkLen / hLen;
	if (dkLen % hLen)
		l++;
	r = dkLen - (l - 1) * hLen;

	/*
	 *     3. For each block of the derived key apply the function F defined
	 *        below to the password P, the salt S, the iteration count c, and
	 *        the block index to compute the block:
	 *
	 *                  T_1 = F (P, S, c, 1) ,
	 *                  T_2 = F (P, S, c, 2) ,
	 *                  ...
	 *                  T_l = F (P, S, c, l) ,
	 *
	 *        where the function F is defined as the exclusive-or sum of the
	 *        first c iterates of the underlying pseudorandom function PRF
	 *        applied to the password P and the concatenation of the salt S
	 *        and the block index i:
	 *
	 *                  F (P, S, c, i) = U_1 \xor U_2 \xor ... \xor U_c
	 *
	 *        where
	 *
	 *                  U_1 = PRF (P, S || INT (i)) ,
	 *                  U_2 = PRF (P, U_1) ,
	 *                  ...
	 *                  U_c = PRF (P, U_{c-1}) .
	 *
	 *        Here, INT (i) is a four-octet encoding of the integer i, most
	 *        significant octet first.
	 *
	 *     4. Concatenate the blocks and extract the first dkLen octets to
	 *        produce a derived key DK:
	 *
	 *                  DK = T_1 || T_2 ||  ...  || T_l<0..r-1>
	 *
	 *     5. Output the derived key DK.
	 *
	 *  Note. The construction of the function F follows a "belt-and-
	 *  suspenders" approach. The iterates U_i are computed recursively to
	 *  remove a degree of parallelism from an opponent; they are exclusive-
	 *  ored together to reduce concerns about the recursion degenerating
	 *  into a small set of values.
	 *
	 */

	if(gcry_md_open(&prf, PRF, GCRY_MD_FLAG_HMAC))
		return -EINVAL;

	if (gcry_md_setkey(prf, P, Plen))
		goto out;

	for (i = 1; (uint) i <= l; i++) {
		memset(T, 0, hLen);

		for (u = 1; u <= c ; u++) {
			gcry_md_reset(prf);

			if (u == 1) {
				memcpy(tmp, S, Slen);
				tmp[Slen + 0] = (i & 0xff000000) >> 24;
				tmp[Slen + 1] = (i & 0x00ff0000) >> 16;
				tmp[Slen + 2] = (i & 0x0000ff00) >> 8;
				tmp[Slen + 3] = (i & 0x000000ff) >> 0;

				gcry_md_write(prf, tmp, tmplen);
			} else {
				gcry_md_write(prf, U, hLen);
			}

			p = gcry_md_read(prf, PRF);
			if (p == NULL)
				goto out;

			memcpy(U, p, hLen);

			for (k = 0; (uint) k < hLen; k++)
				T[k] ^= U[k];

			if (perfcheck && __PBKDF2_performance) {
				rc = 0;
				goto out;
			}

			if (perfcheck)
				__PBKDF2_global_j++;
		}

		memcpy(DK + (i - 1) * hLen, T, (uint) i == l ? r : hLen);
	}
	rc = 0;
out:
	gcry_md_close(prf);
	return rc;
}

int PBKDF2_HMAC(const char *hash,
		const char *password, size_t passwordLen,
		const char *salt, size_t saltLen, unsigned int iterations,
		char *dKey, size_t dKeyLen)
{
	return pkcs5_pbkdf2(hash, password, passwordLen, salt, saltLen,
			    iterations, (unsigned int)dKeyLen, dKey, 0);
}

int PBKDF2_HMAC_ready(const char *hash)
{
	int hash_id = gcry_md_map_name(hash);

	if (!hash_id)
		return -EINVAL;

	/* Used hash must have at least 160 bits */
	if (gcry_md_get_algo_dlen(hash_id) < 20)
		return -EINVAL;

	return 1;
}

static void sigvtalarm(int foo)
{
	__PBKDF2_performance = __PBKDF2_global_j;
}

/* This code benchmarks PBKDF2 and returns iterations/second using wth specified hash */
int PBKDF2_performance_check(const char *hash, uint64_t *iter)
{
	int r;
	char buf;
	struct itimerval it;

	if (__PBKDF2_global_j)
		return -EBUSY;

	if (!PBKDF2_HMAC_ready(hash))
		return -EINVAL;

	signal(SIGVTALRM,sigvtalarm);
	it.it_interval.tv_usec = 0;
	it.it_interval.tv_sec = 0;
	it.it_value.tv_usec = 0;
	it.it_value.tv_sec =  1;
	if (setitimer (ITIMER_VIRTUAL, &it, NULL) < 0)
		return -EINVAL;

	r = pkcs5_pbkdf2(hash, "foo", 3, "bar", 3, ~(0U), 1, &buf, 1);

	*iter = __PBKDF2_performance;
	__PBKDF2_global_j = 0;
	__PBKDF2_performance = 0;
	return r;
}
