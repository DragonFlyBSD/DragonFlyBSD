/*
 * Copyright (c) 2003, Trent Nelson, <trent@arpa.com>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/usr.bin/systat/convtbl.c,v 1.13 2008/01/16 19:27:42 delphij Exp $
 */

#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include "convtbl.h"

#define KILO		(1000LL)
#define MEGA		(KILO * 1000)
#define GIGA		(MEGA * 1000)
#define TERA		(GIGA * 1000)

#define BYTE		(1)
#define BIT		(8)

struct convtbl {
	uintmax_t	 mul;
	uintmax_t	 scale;
	const char	*str;
	const char	*name;
};

static struct convtbl convtbl[] = {
	/* mul, scale, str, name */
	[SC_BYTE] =	{ BYTE, 1, "B", "byte" },
	[SC_KILOBYTE] =	{ BYTE, KILO, "KB", "kbyte" },
	[SC_MEGABYTE] =	{ BYTE, MEGA, "MB", "mbyte" },
	[SC_GIGABYTE] =	{ BYTE, GIGA, "GB", "gbyte" },
	[SC_TERABYTE] =	{ BYTE, TERA, "TB", "tbyte" },
	[SC_AUTOBYTE] =	{ BYTE, 0, "", "autobyte" },

	[SC_BIT] =	{ BIT, 1, "b", "bit" },
	[SC_KILOBIT] =	{ BIT, KILO, "Kb", "kbit" },
	[SC_MEGABIT] =	{ BIT, MEGA, "Mb", "mbit" },
	[SC_GIGABIT] =	{ BIT, GIGA, "Gb", "gbit" },
	[SC_TERABIT] =	{ BIT, TERA, "Tb", "tbit" },
	[SC_AUTOBIT] =	{ BIT, 0, "", "autobit" },

	[SC_AUTO] =	{ 0, 0, "", "auto" }
};

static
struct convtbl *
get_tbl_ptr(const uintmax_t size, const int scale)
{
	uintmax_t	 tmp;
	int		 disp_bits, idx;

	/* If our index is out of range, default to auto-scaling in bits. */
	idx = scale < SC_AUTOBIT ? scale : SC_AUTOBIT;
	disp_bits = idx > SC_AUTOBYTE;

	if (idx == SC_AUTOBYTE || idx == SC_AUTOBIT)
		/*
		 * Simple but elegant algorithm.  Count how many times
		 * we can divide our size value by 1000,
		 * incrementing an index each time.  We then use the
		 * index as the array index into the conversion table.
		 */
		for (tmp = size, idx = disp_bits ? SC_BIT : SC_BYTE;
		     tmp >= 1000 / (disp_bits ? BIT : BYTE) &&
		     idx < (disp_bits ? SC_AUTOBIT : SC_AUTOBYTE) - 1;
		     tmp /= 1000, idx++);

	return (&convtbl[idx]);
}

double
convert(const uintmax_t size, const int scale)
{
	struct convtbl	*tp;

	tp = get_tbl_ptr(size, scale);
	return ((double)size * tp->mul / tp->scale);

}

const char *
get_string(const uintmax_t size, const int scale)
{
	struct convtbl	*tp;

	tp = get_tbl_ptr(size, scale);
	return (tp->str);
}

int
get_scale(const char *name)
{
	int i;

	for (i = 0; i <= SC_AUTO; i++)
		if (strcmp(convtbl[i].name, name) == 0)
			return (i);
	return (-1);
}

const char *
get_helplist(void)
{
	int i;
	size_t len;
	static char *buf;

	if (buf == NULL) {
		len = 0;
		for (i = 0; i <= SC_AUTO; i++)
			len += strlen(convtbl[i].name) + 2;
		if ((buf = malloc(len)) != NULL) {
			buf[0] = '\0';
			for (i = 0; i <= SC_AUTO; i++) {
				strcat(buf, convtbl[i].name);
				if (i < SC_AUTO)
					strcat(buf, ", ");
			}
		} else
			return ("");
	}
	return (buf);
}
