/*
 * Copyright (c) 2000-2002,2005,2008 by Solar Designer.  See LICENSE.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include "passwdqc.h"

/*
 * We separate words in the generated "passphrases" with random special
 * characters out of a set of 8 (so we encode 3 bits per separator
 * character).  To enable the use of our "passphrases" within FTP URLs
 * (and similar), we pick characters that are defined by RFC 3986 as
 * being safe within "userinfo" part of URLs without encoding and
 * without having a special meaning.  Out of those, we avoid characters
 * that are visually ambiguous or difficult over the phone.  This
 * happens to leave us with exactly 8 characters.
 */
#define SEPARATORS			"-_!$&*+="

static int read_loop(int fd, unsigned char *buffer, int count)
{
	int offset, block;

	offset = 0;
	while (count > 0) {
		block = read(fd, &buffer[offset], count);

		if (block < 0) {
			if (errno == EINTR) continue;
			return block;
		}
		if (!block) return offset;

		offset += block;
		count -= block;
	}

	return offset;
}

char *_passwdqc_random(passwdqc_params_t *params)
{
	static char output[0x100];
	int bits;
	int use_separators, count, i;
	unsigned int length, extra;
	char *start, *end;
	int fd;
	unsigned char bytes[2];

	bits = params->random_bits;
	if (bits < 24 || bits > 128)
		return NULL;

	count = 1 + (bits + (14 - 12)) / 15;
	use_separators = ((bits + 11) / 12 != count);

	length = count * 7 - 1;
	if (length >= sizeof(output) || (int)length > params->max)
		return NULL;

	if ((fd = open("/dev/urandom", O_RDONLY)) < 0) return NULL;

	length = 0;
	do {
		if (read_loop(fd, bytes, sizeof(bytes)) != sizeof(bytes)) {
			close(fd);
			return NULL;
		}

		i = (((int)bytes[1] & 0x0f) << 8) | (int)bytes[0];
		start = _passwdqc_wordset_4k[i];
		end = memchr(start, '\0', 6);
		if (!end) end = start + 6;
		extra = end - start;
		if (length + extra >= sizeof(output) - 1) {
			close(fd);
			return NULL;
		}
		memcpy(&output[length], start, extra);
		length += extra;
		bits -= 12;

		if (use_separators && bits > 3) {
			i = ((int)bytes[1] & 0x70) >> 4;
			output[length++] = SEPARATORS[i];
			bits -= 3;
		} else
		if (bits > 0)
			output[length++] = ' ';
	} while (bits > 0);

	memset(bytes, 0, sizeof(bytes));
	output[length] = '\0';

	close(fd);

	return output;
}
