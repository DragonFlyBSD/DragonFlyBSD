/*
 * Copyright (c) 2008 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sbin/hammer/Attic/cmd_prune.c,v 1.2 2008/02/05 07:58:40 dillon Exp $
 */

#include "hammer.h"

static void hammer_prune_load_file(hammer_tid_t now_tid, 
			struct hammer_ioc_prune *prune,
			const char *filesystem, const char *filename);
static int hammer_prune_parse_line(hammer_tid_t now_tid,
			struct hammer_ioc_prune *prune,
			const char *filesystem, char **av, int ac);
static int parse_modulo_time(const char *str, u_int64_t *delta);
static char *tid_to_stamp_str(hammer_tid_t tid);
static void prune_usage(int code);

/*
 * prune <filesystem> from <modulo_time> to <modulo_time> every <modulo_time>
 * prune <filesystem> [using <filename>]
 */
void
hammer_cmd_prune(char **av, int ac)
{
	struct hammer_ioc_prune prune;
	const char *filesystem;
	int fd;
	hammer_tid_t now_tid = (hammer_tid_t)time(NULL) * 1000000000LL;

	bzero(&prune, sizeof(prune));
	prune.nelms = 0;
	prune.beg_obj_id = HAMMER_MIN_OBJID;
	prune.end_obj_id = HAMMER_MAX_OBJID;
	prune.cur_obj_id = prune.end_obj_id;	/* reverse scan */
	prune.cur_key = HAMMER_MAX_KEY;

	if (ac == 0)
		prune_usage(1);
	filesystem = av[0];
	if (ac == 1) {
		hammer_prune_load_file(now_tid, &prune, filesystem, 
				      "/etc/hammer.conf");
	} else if (strcmp(av[1], "using") == 0) {
		if (ac == 2)
			prune_usage(1);
		hammer_prune_load_file(now_tid, &prune, filesystem, av[2]);
	} else {
		if (hammer_prune_parse_line(now_tid, &prune, filesystem,
					    av, ac) < 0) {
			prune_usage(1);
		}
	}
	fd = open(filesystem, O_RDONLY);
	if (fd < 0)
		err(1, "Unable to open %s", filesystem);
	if (ioctl(fd, HAMMERIOC_PRUNE, &prune) < 0)
		printf("Prune %s failed: %s\n", filesystem, strerror(errno));
	else
		printf("Prune %s succeeded\n", filesystem);
	close(fd);
	printf("Pruned %lld records (%lld directory entries) and %lld bytes\n",
		prune.stat_rawrecords,
		prune.stat_dirrecords,
		prune.stat_bytes
	);
}

static void
hammer_prune_load_file(hammer_tid_t now_tid, struct hammer_ioc_prune *prune,
		       const char *filesystem, const char *filename)
{
	char buf[256];
	FILE *fp;
	char *av[16];
	int ac;
	int lineno;

	if ((fp = fopen(filename, "r")) == NULL)
		err(1, "Unable to read %s", filename);
	lineno = 0;
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		++lineno;
		if (strncmp(buf, "prune", 5) != 0)
			continue;
		ac = 0;
		av[ac] = strtok(buf, " \t\r\n");
		while (av[ac] != NULL) {
			++ac;
			if (ac == 16) {
				fclose(fp);
				errx(1, "Malformed prune directive in %s "
				     "line %d\n", filename, lineno);
			}
			av[ac] = strtok(NULL, " \t\r\n");
		}
		if (ac == 0)
			continue;
		if (strcmp(av[0], "prune") != 0)
			continue;
		if (hammer_prune_parse_line(now_tid, prune, filesystem,
					    av + 1, ac - 1) < 0) {
			errx(1, "Malformed prune directive in %s line %d\n",
			     filename, lineno);
		}
	}
	fclose(fp);
}

static __inline
const char *
plural(int notplural)
{
	return(notplural ? "" : "s");
}

/*
 * Parse the following parameters:
 *
 * <filesystem> from <modulo_time> to <modulo_time> every <modulo_time>
 */
static int
hammer_prune_parse_line(hammer_tid_t now_tid, struct hammer_ioc_prune *prune,
			const char *filesystem, char **av, int ac)
{
	struct hammer_ioc_prune_elm *elm;
	u_int64_t from_time;
	u_int64_t to_time;
	u_int64_t every_time;
	char *from_stamp_str;
	char *to_stamp_str;

	if (ac != 7)
		return(-1);
	if (strcmp(av[0], filesystem) != 0)
		return(0);
	if (strcmp(av[1], "from") != 0)
		return(-1);
	if (strcmp(av[3], "to") != 0)
		return(-1);
	if (strcmp(av[5], "every") != 0)
		return(-1);
	if (parse_modulo_time(av[2], &from_time) < 0)
		return(-1);
	if (parse_modulo_time(av[4], &to_time) < 0)
		return(-1);
	if (parse_modulo_time(av[6], &every_time) < 0)
		return(-1);
	if (from_time > to_time)
		return(-1);
	if (from_time == 0 || to_time == 0) {
		fprintf(stderr, "Bad from or to time specification.\n");
		return(-1);
	}
	if (to_time % from_time != 0) {
		fprintf(stderr, "Bad TO time specification.\n"
			"It must be an integral multiple of FROM time\n");
		return(-1);
	}
	if (every_time == 0 ||
	    from_time % every_time != 0 ||
	    to_time % every_time != 0) {
		fprintf(stderr, "Bad 'every <modulo_time>' specification.\n"
			"It must be an integral subdivision of FROM and TO\n");
		return(-1);
	}
	if (prune->nelms == HAMMER_MAX_PRUNE_ELMS) {
		fprintf(stderr, "Too many prune specifications in file! "
			"Max is %d\n", HAMMER_MAX_PRUNE_ELMS);
		return(-1);
	}

	/*
	 * Example:  from 1m to 60m every 5m
	 */
	elm = &prune->elms[prune->nelms++];
	elm->beg_tid = now_tid - now_tid % to_time;
	if (now_tid - elm->beg_tid < to_time)
		elm->beg_tid -= to_time;

	elm->end_tid = now_tid - now_tid % from_time;
	if (now_tid - elm->end_tid < from_time)
		elm->end_tid -= from_time;

	elm->mod_tid = every_time;
	assert(elm->beg_tid < elm->end_tid);

	/*
	 * Convert back to local time for pretty printing
	 */
	from_stamp_str = tid_to_stamp_str(elm->beg_tid);
	to_stamp_str = tid_to_stamp_str(elm->end_tid);
	printf("Prune %s to %s every ", from_stamp_str, to_stamp_str);

	every_time /= 1000000000;
	if (every_time < 60)
		printf("%lld second%s\n", every_time, plural(every_time == 1));
	every_time /= 60;
	if (every_time && every_time < 60)
		printf("%lld minute%s\n", every_time, plural(every_time == 1));
	every_time /= 60;
	if (every_time && every_time < 24)
		printf("%lld hour%s\n", every_time, plural(every_time == 1));
	every_time /= 24;
	if (every_time)
		printf("%lld day%s\n", every_time, plural(every_time == 1));

	free(from_stamp_str);
	free(to_stamp_str);
	return(0);
}

static
int
parse_modulo_time(const char *str, u_int64_t *delta)
{
	char *term;

	*delta = strtoull(str, &term, 10);

	switch(*term) {
	case 'y':
		*delta *= 12;
		/* fall through */
	case 'M':
		*delta *= 30;
		/* fall through */
	case 'd':
		*delta *= 24;
		/* fall through */
	case 'h':
		*delta *= 60;
		/* fall through */
	case 'm':
		*delta *= 60;
		/* fall through */
	case 's':
		break;
	default:
		return(-1);
	}
	*delta *= 1000000000LL;	/* TID's are in nanoseconds */
	return(0);
}

static char *
tid_to_stamp_str(hammer_tid_t tid)
{
	struct tm *tp;
	char *buf = malloc(256);
	time_t t;

	t = (time_t)(tid / 1000000000);
	tp = localtime(&t);
	strftime(buf, 256, "%e-%b-%Y %H:%M:%S %Z", tp);
	return(buf);
}

static void
prune_usage(int code)
{
	fprintf(stderr, "Bad prune directive, specify one of:\n"
			"prune filesystem [using filename]\n"
			"prune filesystem from <modulo_time> to <modulo_time> every <modulo_time>\n");
	exit(code);
}
