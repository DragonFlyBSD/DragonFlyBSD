/*
 * sasc(1) - utility for the `asc' scanner device driver
 * 
 * Copyright (c) 1995 Gunther Schadow.  All rights reserved.
 * Copyright (c) 2004 Liam J. Foy. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Gunther Schadow.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/usr.bin/sasc/sasc.c,v 1.7.2.1 2000/06/30 09:47:52 ps Exp $
 * $DragonFly: src/usr.bin/sasc/sasc.c,v 1.5 2005/01/22 14:01:36 liamfoy Exp $
 *
 */

#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#include <machine/asc_ioctl.h>
#include <machine/limits.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define DEFAULT_FILE "/dev/asc0"

static int	getnum(const char *);
static int	asc_get(int, u_long);
static void	asc_set(int, u_long, int, const char *);
static void	usage(void);

static void
usage(void)
{
	fprintf(stderr,
		"usage: sasc [-sq] [-f file] [-r dpi] [-w width] [-h height]"
		"[-b len] [-t time]\n");
	exit(1);
}

/* Check given numerical arguments */
static int
getnum(const char *str)
{
        long val;
        char *ep;

	errno = 0;
        val = strtol(str, &ep, 10);
        if (errno)
                err(1, "strtol failed: %s", str);

        if (str == ep || *ep != '\0')
                errx(1, "invalid value: %s", str);

	if (val > INT_MAX || val < INT_MIN) {
		errno = ERANGE;
		errc(1, errno, "getnum failed:");
	}

        return((int)val);
}

static void
asc_set(int fd, u_long asc_setting, int asc_value, const char *asc_type)
{
	if (ioctl(fd, asc_setting, &asc_value) < 0)
		err(1, "ioctl failed setting %s(%d)", asc_type, asc_value);

	printf("Successfully set\n");
}

static int
asc_get(int fd, u_long asc_setting)
{
	int asc_value;

	if (ioctl(fd, asc_setting, &asc_value) < 0)
		err(1, "ioctl failed", asc_value);

	return(asc_value);
}

int
main(int argc, char **argv)
{
	const char *file = DEFAULT_FILE;
	int c, fd;
	int show_dpi, show_width, show_height;
	int show_blen, show_btime, show_all;
	int set_blen, set_dpi, set_width;
	int set_height, set_btime, set_switch;

	show_dpi = show_width = show_height = 0;
	show_blen = show_btime = show_all = 0;

	set_blen = set_dpi = set_width = 0;
	set_height = set_btime = set_switch = 0;

	while ((c = getopt(argc, argv, "sqf:b:r:w:h:t:")) != -1) {
		switch (c) {
		case 'f':
			file = optarg;
			break;
		case 'r':
			set_dpi = getnum(optarg);
			break;
		case 'w':
			set_width = getnum(optarg);
			break;
		case 'h':
			set_height = getnum(optarg);
			break;
		case 'b':
			set_blen = getnum(optarg);
			break;
		case 't':
			set_btime = getnum(optarg);
			break;
		case 's':
			set_switch = 1;
			break;
		case 'q':
			show_all = 1;
			break;
		default:
			usage();
		}
	}

	if (argc == 1)
		show_all = 1;

	if ((fd = open(file, O_RDWR)) == -1)
		err(1, "Unable to open: %s", file);

	if (set_switch) {
		if (ioctl(fd, ASC_SRESSW) < 0)
			err(1, "ioctl: ASC_SRESSW failed");
	}

	if (set_dpi)
		asc_set(fd, ASC_SRES, set_dpi, "ASC_SRES");

	if (set_width)
		asc_set(fd, ASC_SWIDTH, set_width, "ASC_SWIDTH");

	if (set_height)
		asc_set(fd, ASC_SHEIGHT, set_height, "ASC_SHEIGHT");

	if (set_blen)
		asc_set(fd, ASC_SBLEN, set_blen, "ASC_SBLEN");

	if (set_btime)
		asc_set(fd, ASC_SBTIME, set_btime, "ASC_SBTIME");

	if (show_all) {
		show_dpi = asc_get(fd, ASC_GRES);
		show_width = asc_get(fd, ASC_GWIDTH);
		show_height = asc_get(fd, ASC_GHEIGHT);
		show_blen = asc_get(fd, ASC_GBLEN);
		show_btime = asc_get(fd, ASC_GBTIME);

		printf("Device: %s\n", file);
		printf("Resolution: %d dpi\n", show_dpi);
		printf("Width: %d\n", show_width);
		printf("Height: %d\n", show_height);
		printf("Buffer length: %d\n", show_blen);
		printf("Buffer timeout: %d\n", show_btime);
	}
	return 0;
}
