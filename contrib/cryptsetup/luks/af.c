/*
 * AFsplitter - Anti forensic information splitter
 * Copyright 2004, Clemens Fruhwirth <clemens@endorphin.org>
 * Copyright (C) 2009 Red Hat, Inc. All rights reserved.
 *
 * AFsplitter diffuses information over a large stripe of data, 
 * therefor supporting secure data destruction.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
 
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <errno.h>
#include <openssl/evp.h>
#include "random.h"

static void XORblock(char const *src1, char const *src2, char *dst, size_t n)
{
	size_t j;

	for(j = 0; j < n; ++j)
		dst[j] = src1[j] ^ src2[j];
}

static int hash_buf(char *src, char *dst, uint32_t iv, int len, const EVP_MD *hash_id)
{
	EVP_MD_CTX mdctx;
	unsigned char digest[128];

	iv = htonl(iv);

	EVP_DigestInit(&mdctx, hash_id);
	EVP_DigestUpdate(&mdctx, (unsigned char *)&iv, sizeof(iv));
	EVP_DigestUpdate(&mdctx, src, len);
	EVP_DigestFinal(&mdctx, digest, NULL);
	memcpy(dst, digest, len);

	return 0;
}

/* diffuse: Information spreading over the whole dataset with
 * the help of hash function.
 */

static int diffuse(char *src, char *dst, size_t size, const EVP_MD *hash_id)
{
	unsigned int digest_size = EVP_MD_size(hash_id);
	unsigned int i, blocks, padding;

	blocks = size / digest_size;
	padding = size % digest_size;

	for (i = 0; i < blocks; i++)
		if(hash_buf(src + digest_size * i,
			    dst + digest_size * i,
			    i, digest_size, hash_id))
			return 1;

	if(padding)
		if(hash_buf(src + digest_size * i,
			    dst + digest_size * i,
			    i, padding, hash_id))
			return 1;

	return 0;
}

/*
 * Information splitting. The amount of data is multiplied by
 * blocknumbers. The same blocksize and blocknumbers values 
 * must be supplied to AF_merge to recover information.
 */

int AF_split(char *src, char *dst, size_t blocksize, unsigned int blocknumbers, const char *hash)
{
	unsigned int i;
	char *bufblock;
	int r = -EINVAL;
	const EVP_MD *hash_id;

	OpenSSL_add_all_digests();
	if (!(hash_id = EVP_get_digestbyname(hash)))
		return -EINVAL;

	if((bufblock = calloc(blocksize, 1)) == NULL) return -ENOMEM;

	/* process everything except the last block */
	for(i=0; i<blocknumbers-1; i++) {
		r = getRandom(dst+(blocksize*i),blocksize);
		if(r < 0) goto out;

		XORblock(dst+(blocksize*i),bufblock,bufblock,blocksize);
		if(diffuse(bufblock, bufblock, blocksize, hash_id))
			goto out;
	}
	/* the last block is computed */
	XORblock(src,bufblock,dst+(i*blocksize),blocksize);
	r = 0;
out:
	free(bufblock);
	return r;
}

int AF_merge(char *src, char *dst, size_t blocksize, unsigned int blocknumbers, const char *hash)
{
	unsigned int i;
	char *bufblock;
	int r = -EINVAL;
	const EVP_MD *hash_id;

	OpenSSL_add_all_digests();
	if (!(hash_id = EVP_get_digestbyname(hash)))
		return -EINVAL;

	if((bufblock = calloc(blocksize, 1)) == NULL) return -ENOMEM;

	memset(bufblock,0,blocksize);
	for(i=0; i<blocknumbers-1; i++) {
		XORblock(src+(blocksize*i),bufblock,bufblock,blocksize);
		if(diffuse(bufblock, bufblock, blocksize, hash_id))
			goto out;
	}
	XORblock(src + blocksize * i, bufblock, dst, blocksize);
	r = 0;
out:
	free(bufblock);
	return 0;
}
