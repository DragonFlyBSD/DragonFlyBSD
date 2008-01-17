/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
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
 * 
 * $DragonFly: src/sbin/hammer/hammer.c,v 1.3 2008/01/17 04:59:48 dillon Exp $
 */

#include "hammer.h"

static void hammer_parsetime(u_int64_t *tidp, const char *timestr);
static void hammer_parsedevs(const char *blkdevs);
static void usage(int exit_code);

int RecurseOpt;

int
main(int ac, char **av)
{
	u_int64_t tid;
	int ch;
	int status;
	char *blkdevs = NULL;

	while ((ch = getopt(ac, av, "hf:r")) != -1) {
		switch(ch) {
		case 'h':
			usage(0);
			/* not reached */
		case 'r':
			RecurseOpt = 1;
			break;
		case 'f':
			blkdevs = optarg;
			break;
		default:
			usage(1);
			/* not reached */
		}
	}
	ac -= optind;
	av += optind;
	if (ac < 1) {
		usage(1);
		/* not reached */
	}

	if (strcmp(av[0], "now") == 0) {
		hammer_parsetime(&tid, "0s");
		printf("0x%08x\n", (int)(tid / 1000000000LL));
		exit(0);
	}
	if (strcmp(av[0], "stamp") == 0) {
		if (av[1] == NULL)
			usage(1);
		hammer_parsetime(&tid, av[1]);
		printf("0x%08x\n", (int)(tid / 1000000000LL));
		exit(0);
	}

	uuid_name_lookup(&Hammer_FSType, "DragonFly HAMMER", &status);
	if (status != uuid_s_ok) {
		errx(1, "uuids file does not have the DragonFly "
			"HAMMER filesystem type");
	}

	init_alist_templates();
	if (strcmp(av[0], "show") == 0) {
		int32_t vol_no = -1;
		int32_t clu_no = -1;

		hammer_parsedevs(blkdevs);
		if (ac > 1)
			sscanf(av[1], "%d:%d", &vol_no, &clu_no);
		hammer_cmd_show(vol_no, clu_no, 0);
		exit(0);
	}
	usage(1);
	/* not reached */
	return(0);
}

/*
 * Parse a timestamp for the mount point
 *
 * yyyymmddhhmmss
 * -N[s/h/d/m/y]
 */
static
void
hammer_parsetime(u_int64_t *tidp, const char *timestr)
{
	struct tm tm;
	time_t t;
	int32_t n;
	char c;
	double seconds = 0;

	t = time(NULL);

	if (*timestr == 0)
		usage(1);

	if (isalpha(timestr[strlen(timestr)-1])) {
		if (sscanf(timestr, "%d%c", &n, &c) != 2)
			usage(1);
		switch(c) {
		case 'Y':
			n *= 365;
			goto days;
		case 'M':
			n *= 30;
			/* fall through */
		case 'D':
		days:
			n *= 24;
			/* fall through */
		case 'h':
			n *= 60;
			/* fall through */
		case 'm':
			n *= 60;
			/* fall through */
		case 's':
			t -= n;
			break;
		default:
			usage(1);
		}
	} else {
		localtime_r(&t, &tm);
		seconds = (double)tm.tm_sec;
		tm.tm_year -= 1900;
		tm.tm_mon -= 1;
		n = sscanf(timestr, "%4d%2d%2d:%2d%2d%lf",
			   &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
			   &tm.tm_hour, &tm.tm_min, &seconds);
		tm.tm_mon += 1;
		tm.tm_year += 1900;
		tm.tm_sec = (int)seconds;
		t = mktime(&tm);
	}
	localtime_r(&t, &tm);
	*tidp = (u_int64_t)t * 1000000000 + 
		(seconds - (int)seconds) * 1000000000;
}

static
void
hammer_parsedevs(const char *blkdevs)
{
	char *copy;
	char *volname;

	if (blkdevs == NULL) {
		errx(1, "A -f blkdevs specification is required "
			"for this command");
	}

	copy = strdup(blkdevs);
	while ((volname = copy) != NULL) {
		if ((copy = strchr(copy, ':')) != NULL)
			*copy++ = 0;
		setup_volume(-1, volname, 0, O_RDONLY);
	}
}

static
void
usage(int exit_code)
{
	fprintf(stderr, 
		"hammer -h\n"
		"hammer now\n"
		"hammer stamp <time>\n"
		"hammer -f blkdevs [-r] show [vol_no[:clu_no]]\n"
	);
	fprintf(stderr, "    time: +n[s/m/h/D/M/Y]\n"
			"    time: yyyymmdd[:hhmmss]\n");
	exit(exit_code);
}

