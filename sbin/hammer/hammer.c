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
 * $DragonFly: src/sbin/hammer/hammer.c,v 1.13 2008/03/25 03:57:58 dillon Exp $
 */

#include "hammer.h"
#include <math.h>

static void hammer_parsetime(u_int64_t *tidp, const char *timestr);
static void hammer_waitsync(int dosleep);
static void hammer_parsedevs(const char *blkdevs);
static void usage(int exit_code);

int RecurseOpt;
int VerboseOpt;
int NoSyncOpt;
const char *LinkPath;

int
main(int ac, char **av)
{
	struct timeval tv;
	u_int64_t tid;
	int ch;
	u_int32_t status;
	char *blkdevs = NULL;

	while ((ch = getopt(ac, av, "hf:rs:vx")) != -1) {
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
		case 's':
			LinkPath = optarg;
			break;
		case 'v':
			++VerboseOpt;
			break;
		case 'x':
			++NoSyncOpt;
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
		hammer_waitsync(1);
		tid = (hammer_tid_t)time(NULL) * 1000000000LLU;
		printf("0x%08x\n", (int)(tid / 1000000000LL));
		exit(0);
	}
	if (strcmp(av[0], "now64") == 0) {
		hammer_waitsync(0);
		gettimeofday(&tv, NULL);
		tid = (hammer_tid_t)tv.tv_sec * 1000000000LLU +
			tv.tv_usec * 1000LLU;
		printf("0x%016llx\n", tid);
		exit(0);
	}
	if (strcmp(av[0], "stamp") == 0) {
		if (av[1] == NULL)
			usage(1);
		hammer_parsetime(&tid, av[1]);
		printf("0x%08x\n", (int)(tid / 1000000000LL));
		exit(0);
	}
	if (strcmp(av[0], "stamp64") == 0) {
		if (av[1] == NULL)
			usage(1);
		hammer_parsetime(&tid, av[1]);
		printf("0x%016llx\n", tid);
		exit(0);
	}
	if (strcmp(av[0], "namekey") == 0) {
		int64_t key;

		if (av[1] == NULL)
			usage(1);
		key = (int64_t)(crc32(av[1], strlen(av[1])) & 0x7FFFFFFF) << 32;
		if (key == 0)
			key |= 0x100000000LL;
		printf("0x%016llx\n", key);
		exit(0);
	}
	if (strcmp(av[0], "namekey32") == 0) {
		int32_t key;

		if (av[1] == NULL)
			usage(1);
		key = crc32(av[1], strlen(av[1])) & 0x7FFFFFFF;
		if (key == 0)
			++key;
		printf("0x%08x\n", key);
		exit(0);
	}
	if (strcmp(av[0], "prune") == 0) {
		hammer_cmd_prune(av + 1, ac - 1);
		exit(0);
	}

	if (strncmp(av[0], "history", 7) == 0) {
		hammer_cmd_history(av[0] + 7, av + 1, ac - 1);
		exit(0);
	}
	if (strcmp(av[0], "reblock") == 0) {
		hammer_cmd_reblock(av + 1, ac - 1);
		exit(0);
	}

	uuid_name_lookup(&Hammer_FSType, "DragonFly HAMMER", &status);
	if (status != uuid_s_ok) {
		errx(1, "uuids file does not have the DragonFly "
			"HAMMER filesystem type");
	}

	if (strcmp(av[0], "show") == 0) {
		hammer_off_t node_offset = (hammer_off_t)-1;

		hammer_parsedevs(blkdevs);
		if (ac > 1)
			sscanf(av[1], "%llx", &node_offset);
		hammer_cmd_show(node_offset, 0, NULL, NULL);
		exit(0);
	}
	if (strcmp(av[0], "blockmap") == 0) {
		hammer_parsedevs(blkdevs);
		hammer_cmd_blockmap();
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
	struct timeval tv;
	struct tm tm;
	int32_t n;
	char c;

	gettimeofday(&tv, NULL);

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
			tv.tv_sec -= n;
			break;
		default:
			usage(1);
		}
	} else {
		double seconds = 0;

		localtime_r(&tv.tv_sec, &tm);
		seconds = (double)tm.tm_sec;
		tm.tm_year += 1900;
		tm.tm_mon += 1;
		n = sscanf(timestr, "%4d%2d%2d:%2d%2d%lf",
			   &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
			   &tm.tm_hour, &tm.tm_min, &seconds);
		tm.tm_mon -= 1;
		tm.tm_year -= 1900;
		/* if [:hhmmss] is omitted, assume :000000.0 */
		if (n < 4)
			tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
		else
			tm.tm_sec = (int)seconds;
		tv.tv_sec = mktime(&tm);
		tv.tv_usec = (int)((seconds - floor(seconds)) * 1000000.0);
	}
	*tidp = (u_int64_t)tv.tv_sec * 1000000000LLU + 
		tv.tv_usec * 1000LLU;
}

/*
 * If the TID is within 60 seconds of the current time we sync().  If
 * dosleep is non-zero and the TID is within 1 second of the current time
 * we wait for the second-hand to turn over.
 *
 * The NoSyncOpt prevents both the sync() call and any sleeps from occuring.
 */
static
void
hammer_waitsync(int dosleep)
{
	time_t t1, t2;

	if (NoSyncOpt == 0) {
		sync();
		t1 = t2 = time(NULL);
		while (dosleep && t1 == t2) {
			usleep(100000);
			t2 = time(NULL);
		}
	}
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
		"hammer [-x] now[64]\n"
		"hammer stamp[64] <time>\n"
		"hammer [-s linkpath] prune <filesystem> [using <configfile>]\n"
		"hammer [-s linkpath] prune <filesystem> from <modulo_time> to "
				"<modulo_time> every <modulo_time>\n"
		"hammer prune <filesystem> everything\n"
		"hammer reblock <filesystem> [compact%%] (default 90%%)\n"
		"hammer history[@offset[,len]] <file-1>...<file-N>\n"
		"hammer -f blkdevs [-r] show\n"
		"hammer -f blkdevs blockmap\n"
	);
	fprintf(stderr, "time: +n[s/m/h/D/M/Y]\n"
			"time: yyyymmdd[:hhmmss]\n"
			"modulo_time: n{s,m,h,d,M,y}\n");
	exit(exit_code);
}

