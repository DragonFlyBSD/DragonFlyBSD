/*
 * Copyright (c) 2017 The DragonFly Project.  All rights reserved.
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

#include "kcollect.h"

#define SLEEP_INTERVAL	60	/* minimum is KCOLLECT_INTERVAL */

static void dump_text(kcollect_t *ary, size_t count, size_t total_count);
static void dump_dbm(kcollect_t *ary, size_t count, const char *datafile);
static void dump_fields(kcollect_t *ary);
static void adjust_fields(kcollect_t *ent, const char *fields);

FILE *OutFP;
int UseGMT;
int OutputWidth = 1024;
int OutputHeight = 1024;

int
main(int ac, char **av)
{
	kcollect_t *ary;
	size_t bytes = 0;
	size_t count;
	size_t total_count;
	const char *datafile = NULL;
	const char *fields = NULL;
	int cmd = 't';
	int ch;
	int keepalive = 0;
	int last_ticks;
	int loops = 0;

	OutFP = stdout;

	sysctlbyname("kern.collect_data", NULL, &bytes, NULL, 0);
	if (bytes == 0) {
		fprintf(stderr, "kern.collect_data not available\n");
		exit(1);
	}

	while ((ch = getopt(ac, av, "o:b:flgxw:GW:H:")) != -1) {
		switch(ch) {
		case 'o':
			fields = optarg;
			break;
		case 'b':
			datafile = optarg;
			cmd = 'b';
			break;
		case 'f':
			keepalive = 1;
			break;
		case 'l':
			cmd = 'l';
			break;
		case 'w':
			datafile = optarg;
			cmd = 'w';
			break;
		case 'g':
			cmd = 'g';
			break;
		case 'x':
			cmd = 'x';
			break;
		case 'G':
			UseGMT = 1;
			break;
		case 'W':
			OutputWidth = strtol(optarg, NULL, 0);
			break;
		case 'H':
			OutputHeight = strtol(optarg, NULL, 0);
			break;
		default:
			fprintf(stderr, "Unknown option %c\n", ch);
			exit(1);
			/* NOT REACHED */
		}
	}
	if (cmd != 'x' && ac != optind) {
		fprintf(stderr, "Unknown argument %s\n", av[optind]);
		exit(1);
		/* NOT REACHED */
	}

	total_count = 0;
	last_ticks = 0;

	if (cmd == 'x' || cmd == 'w')
		start_gnuplot(ac - optind, av + optind);

	do {
		/*
		 * Snarf as much data as we can.  If we are looping,
		 * snarf less (no point snarfing stuff we already have).
		 */
		bytes = 0;
		sysctlbyname("kern.collect_data", NULL, &bytes, NULL, 0);
		if (cmd == 'l')
			bytes = sizeof(kcollect_t) * 2;

		if (loops) {
			size_t loop_bytes;

			loop_bytes = sizeof(kcollect_t) *
				     (4 + SLEEP_INTERVAL / KCOLLECT_INTERVAL);
			if (bytes > loop_bytes)
				bytes = loop_bytes;
		}

		ary = malloc(bytes);
		sysctlbyname("kern.collect_data", ary, &bytes, NULL, 0);

		if (fields)
			adjust_fields(&ary[1], fields);

		count = bytes / sizeof(kcollect_t);
		if (loops) {
			while (count > 2) {
				if ((int)(ary[count-1].ticks - last_ticks) > 0)
					break;
				--count;
			}
		}

		switch(cmd) {
		case 't':
			if (count > 2)
				dump_text(ary, count, total_count);
			break;
		case 'b':
			if (count > 2)
				dump_dbm(ary, count, datafile);
			break;
		case 'l':
			dump_fields(ary);
			exit(0);
			break;		/* NOT REACHED */
		case 'g':
			if (count > 2)
				dump_gnuplot(ary, count, NULL);
			break;
		case 'w':
			if (count >= 2)
				dump_gnuplot(ary, count, datafile);
			break;
		case 'x':
			if (count > 2)
				dump_gnuplot(ary, count, NULL);
			break;
		}
		if (keepalive) {
			fflush(OutFP);
			fflush(stdout);
			switch(cmd) {
			case 't':
				sleep(1);
				break;
			case 'x':
			case 'g':
			case 'w':
				sleep(60);
				break;
			default:
				sleep(10);
				break;
			}
		}
		last_ticks = ary[2].ticks;
		if (count >= 2)
			total_count += count - 2;

		/*
		 * Loop for incremental aquisition.  When outputting to
		 * gunplot, we have to send the whole data-set again so
		 * do not increment loops in that case.
		 */
		if (cmd != 'g' && cmd != 'x' && cmd != 'w')
			++loops;

		free(ary);
	} while (keepalive);

	if (cmd == 'x')
		pclose(OutFP);
}

static
void
dump_text(kcollect_t *ary, size_t count, size_t total_count)
{
	int j;
	int i;
	uintmax_t scale;
	uintmax_t value;
	char fmt;
	char buf[9];
	struct tm *tmv;
	time_t t;

	for (i = count - 1; i >= 2; --i) {
		if ((total_count & 15) == 0) {
			printf("%8.8s", "time");
			for (j = 0; j < KCOLLECT_ENTRIES; ++j) {
				if (ary[1].data[j]) {
					printf(" %8.8s",
						(char *)&ary[1].data[j]);
				}
			}
			printf("\n");
		}

		/*
		 * Timestamp
		 */
		t = ary[i].realtime.tv_sec;
		if (UseGMT)
			tmv = gmtime(&t);
		else
			tmv = localtime(&t);
		strftime(buf, sizeof(buf), "%H:%M:%S", tmv);
		printf("%8.8s", buf);

		for (j = 0; j < KCOLLECT_ENTRIES; ++j) {
			if (ary[1].data[j] == 0)
				continue;

			/*
			 * NOTE: kernel does not have to provide the scale
			 *	 (that is, the highest likely value), nor
			 *	 does it make sense in all cases.
			 *
			 *	 Example scale - kernel provides total amount
			 *	 of memory available for memory related
			 *	 statistics in the scale field.
			 */
			value = ary[i].data[j];
			scale = KCOLLECT_GETSCALE(ary[0].data[j]);
			fmt = KCOLLECT_GETFMT(ary[0].data[j]);

			printf(" ");

			switch(fmt) {
			case '2':
				/*
				 * fractional x100
				 */
				printf("%5ju.%02ju", value / 100, value % 100);
				break;
			case 'p':
				/*
				 * Percentage fractional x100 (100% = 10000)
				 */
				printf("%4ju.%02ju%%",
					value / 100, value % 100);
				break;
			case 'm':
				/*
				 * Megabytes
				 */
				humanize_number(buf, sizeof(buf), value, "",
						2,
						HN_FRACTIONAL |
						HN_NOSPACE);
				printf("%8.8s", buf);
				break;
			case 'c':
				/*
				 * Raw count over period (this is not total)
				 */
				humanize_number(buf, sizeof(buf), value, "",
						HN_AUTOSCALE,
						HN_FRACTIONAL |
						HN_NOSPACE |
						HN_DIVISOR_1000);
				printf("%8.8s", buf);
				break;
			case 'b':
				/*
				 * Total bytes (this is a total), output
				 * in megabytes.
				 */
				if (scale > 100000000) {
					humanize_number(buf, sizeof(buf),
							value, "",
							3,
							HN_FRACTIONAL |
							HN_NOSPACE);
				} else {
					humanize_number(buf, sizeof(buf),
							value, "",
							2,
							HN_FRACTIONAL |
							HN_NOSPACE);
				}
				printf("%8.8s", buf);
				break;
			default:
				printf("        ");
				break;
			}
		}
		printf("\n");
		++total_count;
	}
}

static void
dump_dbm(kcollect_t *ary __unused, size_t count __unused, const char *datafile __unused)
{
}

static void
dump_fields(kcollect_t *ary)
{
	int j;

	for (j = 0; j < KCOLLECT_ENTRIES; ++j) {
		if (ary[1].data[j] == 0)
			continue;
		printf("%8.8s %c\n",
		       (char *)&ary[1].data[j],
		       KCOLLECT_GETFMT(ary[0].data[j]));
	}
}

static void
adjust_fields(kcollect_t *ent, const char *fields)
{
	char *copy = strdup(fields);
	char *word;
	int selected[KCOLLECT_ENTRIES];
	int j;

	bzero(selected, sizeof(selected));

	word = strtok(copy, ", \t");
	while (word) {
		for (j = 0; j < KCOLLECT_ENTRIES; ++j) {
			if (strncmp(word, (char *)&ent->data[j], 8) == 0) {
				selected[j] = 1;
				break;
			}
		}
		word = strtok(NULL, ", \t");
	}
	free(copy);
	for (j = 0; j < KCOLLECT_ENTRIES; ++j) {
		if (!selected[j])
			ent->data[j] = 0;
	}
}
