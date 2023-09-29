/*
 * Copyright (c) 2023 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
 * by Venkatesh Srinivas <vsrinivas@dragonflybsd.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "hammer2.h"
#include "lz4/hammer2_lz4.h"
#include "zlib/hammer2_zlib.h"

#define DEBUFSIZE	HAMMER2_PBUFSIZE

static char OutBuf[DEBUFSIZE + 128];	/* allow for some overflow */

void *
hammer2_decompress_LZ4(void *inbuf, size_t insize, size_t outsize,
		       int *statusp)
{
	int result;
	int cinsize;

	assert(outsize <= DEBUFSIZE);
	*statusp = 0;

	cinsize = *(const int *)inbuf;
	if (cinsize > (int)insize) {
		printf("FAIL1\n");
		bzero(OutBuf, outsize);
		return OutBuf;
	}

	result = LZ4_decompress_safe((char *)inbuf + 4, OutBuf,
				     cinsize, outsize);
	if (result < 0) {
		bzero(OutBuf, outsize);
	} else {
		*statusp = 1;
		if (result < (int)outsize)
			bzero(OutBuf + result, outsize - result);
	}
	if (result < 0)
		printf("LZ4 decompression failure\n");

	return OutBuf;
}

void *
hammer2_decompress_ZLIB(void *inbuf, size_t insize, size_t outsize,
			int *statusp)
{
	z_stream strm_decompress;
	int ret;

	assert(outsize <= DEBUFSIZE);
	*statusp = 0;

	bzero(&strm_decompress, sizeof(strm_decompress));
	strm_decompress.avail_in = 0;
	strm_decompress.next_in = Z_NULL;
	ret = inflateInit(&strm_decompress);

	if (ret != Z_OK) {
		bzero(OutBuf, outsize);
		printf("ZLIB1 decompression failure\n");
	} else {
		strm_decompress.next_in = inbuf;
		strm_decompress.next_out = OutBuf;
		strm_decompress.avail_in = insize;
		strm_decompress.avail_out = outsize;
		ret = inflate(&strm_decompress, Z_FINISH);
		if (ret != Z_STREAM_END) {
			bzero(OutBuf, outsize);
			printf("ZLIB2 decompression failure\n");
		} else {
			ret = outsize - strm_decompress.avail_out;
			if (ret < (int)outsize)
				bzero(OutBuf + ret, strm_decompress.avail_out);
			*statusp = 1;
		}
		ret = inflateEnd(&strm_decompress);
	}
	return OutBuf;
}
