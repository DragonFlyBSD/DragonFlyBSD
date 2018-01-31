/*
 * Copyright (c) 2011 Alex Hornung <alex@alexhornung.com>.
 * All rights reserved.
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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <ctype.h>
#include <errno.h>

#include "humanize.h"

static const char prefixes[] = " KMGTPE";
int
_humanize_number(char *buf, size_t bufsz, uint64_t num)
{
	const char *prefixp;
	int ret;
	uint64_t i, d;

	prefixp = prefixes;
	i = num;
	d = 0;

	while ((i > 1024) && (*prefixp != '\0')) {
		d = (i % 1024)/10;
		i /= 1024;
		++prefixp;
	}

	if (d > 0)
		ret = snprintf(buf, bufsz, "%"PRIu64".%"PRIu64" %c",
		    i, d, *prefixp);
	else
		ret = snprintf(buf, bufsz, "%"PRIu64" %c", i, *prefixp);


	if ((ret < 0) || ((size_t)ret >= bufsz)) {
		errno = ENOMEM;
		return -1;
	} else {
		return 0;
	}
}

int
_dehumanize_number(const char *buf, uint64_t *dest)
{
	char *endptr;
	uint64_t n, n_check, d;
	uint64_t multiplier;
	size_t len;

	if (*buf == '\0') {
		errno = EINVAL;
		return -1;
	}

	len = strlen(buf);
	if (tolower(buf[len-1]) == 'b')
		--len;

	multiplier = 1;

	switch (tolower(buf[len-1])) {
	case 'y':
		multiplier *= 1024;
		/* FALLTHROUGH */
	case 'z':
		multiplier *= 1024;
		/* FALLTHROUGH */
	case 'e':
		multiplier *= 1024;
		/* FALLTHROUGH */
	case 'p':
		multiplier *= 1024;
		/* FALLTHROUGH */
	case 't':
		multiplier *= 1024;
		/* FALLTHROUGH */
	case 'g':
		multiplier *= 1024;
		/* FALLTHROUGH */
	case 'm':
		multiplier *= 1024;
		/* FALLTHROUGH */
	case 'k':
		multiplier *= 1024;
		break;
	default:
		/*
		 * only set error if string ends in a character that
		 * is not a valid unit.
		 */
		if (isalpha(buf[len-1])) {
			errno = EINVAL;
			return -1;
		}
	}

	d = 0;
	n = n_check = strtoull(buf, &endptr, 10);
	if (endptr) {
		if ((*endptr != '.') && (*endptr != '\0') &&
		    (*endptr != ' ') && (endptr != &buf[len-1])) {
			errno = EINVAL;
			return -1;
		}

		if (*endptr == '.') {
			d = strtoull(endptr+1, &endptr, 10);
			if (endptr && (*endptr != '\0') &&
			    (*endptr != ' ') &&
			    (endptr != &buf[len-1])) {
				errno = EINVAL;
				return -1;
			}
		}
	}

	if (d != 0) {
		while (d < 100)
			d *= 10;

		while (d > 1000)
			d /= 10;
	}

	d *= (multiplier/1024);
	n *= multiplier;

	if ((uint64_t)(n/multiplier) != n_check) {
		errno = ERANGE;
		return -1;
	}

	n += d;
	*dest = n;

	return 0;
}

#ifdef __TEST_HUMANIZE__

#include <assert.h>
int main(int argc, char *argv[])
{
	char buf[1024];
	uint64_t n, out;
	int err;

	if (argc < 3)
		return -1;

	n = strtoull(argv[1], NULL, 10);

	err = _humanize_number(buf, 1024, n);
	assert(err == 0);

	err = _dehumanize_number(buf, &out);
	assert(err == 0);

	printf("Converting: %"PRIu64" => %s => %"PRIu64"\n", n, buf, out);

	err = _dehumanize_number(argv[2], &out);
	assert (err == 0);

	printf("Converting: %s => %"PRIu64"\n", argv[2], out);

	return 0;
}

#endif
