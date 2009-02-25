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
 * $DragonFly: src/usr.bin/undo/undo.c,v 1.6 2008/07/17 21:34:47 thomas Exp $
 */
/*
 * UNDO - retrieve an older version of a file.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/tree.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <vfs/hammer/hammer_disk.h>
#include <vfs/hammer/hammer_ioctl.h>

/*
 * Sorted list of transaction ids
 */
struct undo_hist_entry;
RB_HEAD(undo_hist_entry_rb_tree, undo_hist_entry);
RB_PROTOTYPE2(undo_hist_entry_rb_tree, undo_hist_entry, rbnode,
	undo_hist_entry_compare, hammer_tid_t);

struct undo_hist_entry {
	RB_ENTRY(undo_hist_entry) rbnode;
	struct hammer_ioc_hist_entry tse;
};

enum undo_type { TYPE_FILE, TYPE_DIFF, TYPE_RDIFF, TYPE_HISTORY };

static int undo_hist_entry_compare(struct undo_hist_entry *he1,
		    struct undo_hist_entry *he2);
static void doiterate(const char *orig_filename, const char *outFileName,
		   const char *outFilePostfix, int mult, enum undo_type type);
static void dogenerate(const char *filename, const char *outFileName,
		   const char *outFilePostfix,
		   int mult, int idx, enum undo_type type,
		   struct hammer_ioc_hist_entry ts1,
		   struct hammer_ioc_hist_entry ts2,
		   int force);
static struct hammer_ioc_hist_entry
	    find_recent(const char *filename);
static struct hammer_ioc_hist_entry
	    output_history(const char *filename, int fd, FILE *fp,
		   struct undo_hist_entry_rb_tree *tse_tree);
static struct hammer_ioc_hist_entry
	    collect_history(int fd, int *error,
		   struct undo_hist_entry_rb_tree *tse_tree);
static hammer_tid_t parse_delta_time(const char *timeStr);
static void runcmd(int fd, const char *cmd, ...);
static char *timestamp(hammer_ioc_hist_entry_t hen);
static void usage(void);

static int VerboseOpt;

RB_GENERATE2(undo_hist_entry_rb_tree, undo_hist_entry, rbnode,
	undo_hist_entry_compare, hammer_tid_t, tse.tid);


int
main(int ac, char **av)
{
	const char *outFileName = NULL;
	const char *outFilePostfix = NULL;
	enum { CMD_DUMP, CMD_ITERATEALL } cmd;
	enum undo_type type;
	struct hammer_ioc_hist_entry ts1;
	struct hammer_ioc_hist_entry ts2;
	int c;
	int mult;

	bzero(&ts1, sizeof(ts1));
	bzero(&ts2, sizeof(ts2));

	cmd = CMD_DUMP;
	type = TYPE_FILE;

	while ((c = getopt(ac, av, "adDiuvo:t:")) != -1) {
		switch(c) {
		case 'd':
			if (type != TYPE_FILE)
				usage();
			type = TYPE_DIFF;
			break;
		case 'D':
			if (type != TYPE_FILE)
				usage();
			type = TYPE_RDIFF;
			break;
		case 'i':
			if (type != TYPE_FILE)
				usage();
			type = TYPE_HISTORY;
			break;
		case 'a':
			cmd = CMD_ITERATEALL;
			break;
		case 'u':
			outFilePostfix = ".undo";
			break;
		case 'v':
			++VerboseOpt;
			break;
		case 'o':
			outFileName = optarg;
			break;
		case 't':
			if (ts1.tid && ts2.tid)
				usage();
			else if (ts1.tid == 0)
				ts1.tid = parse_delta_time(optarg);
			else
				ts2.tid = parse_delta_time(optarg);
			break;
		default:
			usage();
			/* NOT REACHED */
			break;
		}
	}

	/*
	 * Option validation
	 */
	if (outFileName && outFilePostfix) {
		fprintf(stderr, "The -o option may not be combined with -u\n");
		usage();
	}

	ac -= optind;
	av += optind;
	mult = (ac > 1);

	if (ac == 0)
		usage();

	/*
	 * Validate the output template, if specified.
	 */
	if (outFileName && mult) {
		const char *ptr = outFileName;
		int didStr = 0;

		while ((ptr = strchr(ptr, '%')) != NULL) {
			if (ptr[1] == 's') {
				if (didStr) {
					fprintf(stderr, "Malformed output "
							"template\n");
					usage();
				}
				didStr = 1;
				++ptr;
			} else if (ptr[1] != '%') {
				fprintf(stderr, "Malformed output template\n");
				usage();
			} else {
				ptr += 2;
			}
		}
	}

	while (ac) {
		switch(cmd) {
		case CMD_DUMP:
			dogenerate(*av, outFileName, outFilePostfix,
				   mult, -1, type, ts1, ts2, 1);
			break;
		case CMD_ITERATEALL:
			doiterate(*av, outFileName, outFilePostfix,
				  mult, type);
			break;
		}
		++av;
		--ac;
	}
	return(0);
}

/*
 * Iterate through a file's history
 */
static
void
doiterate(const char *orig_filename, const char *outFileName,
	   const char *outFilePostfix, int mult, enum undo_type type)
{
	struct undo_hist_entry_rb_tree tse_tree;
	struct undo_hist_entry *tse1;
	struct undo_hist_entry *tse2;
	struct hammer_ioc_hist_entry tid_max;
	struct hammer_ioc_hist_entry ts1;
	const char *use_filename;
	char *path = NULL;
	int i;
	int fd;

	RB_INIT(&tse_tree);

	tid_max.tid = HAMMER_MAX_TID;
	tid_max.time32 = 0;

	use_filename = orig_filename;
	if ((fd = open(orig_filename, O_RDONLY)) < 0) {
		ts1 = find_recent(orig_filename);
		if (ts1.tid) {
			asprintf(&path, "%s@@0x%016llx",
				 orig_filename, ts1.tid);
			use_filename = path;
		}
	}

	if ((fd = open(use_filename, O_RDONLY)) >= 0) {
		printf("%s: ITERATE ENTIRE HISTORY\n", orig_filename);
		output_history(NULL, fd, NULL, &tse_tree);
		close(fd);

		tse1 = NULL;
		i = 0;
		RB_FOREACH(tse2, undo_hist_entry_rb_tree, &tse_tree) {
			if (tse1) {
				dogenerate(orig_filename,
					   outFileName, outFilePostfix,
					   mult, i, type,
					   tse1->tse, tse2->tse, 0);
			}
			tse1 = tse2;
			++i;
		}
		if (!RB_EMPTY(&tse_tree)) {
			dogenerate(orig_filename,
				   outFileName, outFilePostfix,
				   mult, i, type,
				   tse1->tse, tid_max, 0);
		}
	} else {
		printf("%s: ITERATE ENTIRE HISTORY: %s\n",
			orig_filename, strerror(errno));
	}
	if (path)
		free(path);
}

/*
 * Generate output for a file as-of ts1 (ts1 may be 0!), if diffing then
 * through ts2.
 */
static
void
dogenerate(const char *filename, const char *outFileName,
	   const char *outFilePostfix,
	   int mult, int idx, enum undo_type type,
	   struct hammer_ioc_hist_entry ts1,
	   struct hammer_ioc_hist_entry ts2,
	   int force)
{
	struct stat st;
	const char *elm;
	char *ipath1 = NULL;
	char *ipath2 = NULL;
	FILE *fi;
	FILE *fp; 
	char *buf;
	char *path;
	int n;

	buf = malloc(8192);

	/*
	 * Open the input file.  If ts1 is 0 try to locate the most recent
	 * version of the file prior to the current version.
	 */
	if (ts1.tid == 0)
		ts1 = find_recent(filename);
	if (ts1.tid == 0)
		asprintf(&ipath1, "%s", filename);
	else
		asprintf(&ipath1, "%s@@0x%016llx", filename, ts1.tid);

	if (ts2.tid == 0)
		asprintf(&ipath2, "%s", filename);
	else
		asprintf(&ipath2, "%s@@0x%016llx", filename, ts2.tid);

	if (lstat(ipath1, &st) < 0 && lstat(ipath2, &st) < 0) {
		if (force == 0 || VerboseOpt) {
			fprintf(stderr, "Unable to access either %s or %s\n",
				ipath1, ipath2);
		}
		free(ipath1);
		free(ipath2);
		goto done;
	}

	/*
	 * elm is the last component of the input file name
	 */
	if ((elm = strrchr(filename, '/')) != NULL)
		++elm;
	else
		elm = filename;

	/*
	 * Where do we stuff our output?
	 */
	if (outFileName) {
		if (mult) {
			asprintf(&path, outFileName, elm);
			fp = fopen(path, "w");
			if (fp == NULL) {
				perror(path);
				exit(1);
			}
			free(path);
		} else {
			fp = fopen(outFileName, "w");
			if (fp == NULL) {
				perror(outFileName);
				exit(1);
			}
		}
	} else if (outFilePostfix) {
		if (idx >= 0) {
			asprintf(&path, "%s%s.%04d", filename,
				 outFilePostfix, idx);
		} else {
			asprintf(&path, "%s%s", filename, outFilePostfix);
		}
		fp = fopen(path, "w");
		if (fp == NULL) {
			perror(path);
			exit(1);
		}
		free(path);
	} else {
		if (mult && type == TYPE_FILE) {
			if (idx >= 0) {
				printf("\n>>> %s %04d 0x%016llx %s\n\n",
				       filename, idx, ts1.tid, timestamp(&ts1));
			} else {
				printf("\n>>> %s ---- 0x%016llx %s\n\n",
				       filename, ts1.tid, timestamp(&ts1));
			}
		} else if (idx >= 0 && type == TYPE_FILE) {
			printf("\n>>> %s %04d 0x%016llx %s\n\n", 
			       filename, idx, ts1.tid, timestamp(&ts1));
		}
		fp = stdout;
	}

	switch(type) {
	case TYPE_FILE:
		if ((fi = fopen(ipath1, "r")) != NULL) {
			while ((n = fread(buf, 1, 8192, fi)) > 0)
				fwrite(buf, 1, n, fp);
			fclose(fi);
		}
		break;
	case TYPE_DIFF:
		printf("diff -N -r -u %s %s (to %s)\n",
		       ipath1, ipath2, timestamp(&ts2));
		fflush(stdout);
		runcmd(fileno(fp), "/usr/bin/diff", "diff", "-N", "-r", "-u", ipath1, ipath2, NULL);
		break;
	case TYPE_RDIFF:
		printf("diff -N -r -u %s %s\n", ipath2, ipath1);
		fflush(stdout);
		runcmd(fileno(fp), "/usr/bin/diff", "diff", "-N", "-r", "-u", ipath2, ipath1, NULL);
		break;
	case TYPE_HISTORY:
		if ((fi = fopen(ipath1, "r")) != NULL) {
			output_history(filename, fileno(fi), fp, NULL);
			fclose(fi);
		}
		break;
	}

	if (fp != stdout)
		fclose(fp);
done:
	free(buf);
}

/*
 * Try to find a recent version of the file.
 *
 * XXX if file cannot be found
 */
static
struct hammer_ioc_hist_entry
find_recent(const char *filename)
{
	struct undo_hist_entry_rb_tree tse_tree;
	struct undo_hist_entry *tse;
	struct hammer_ioc_hist_entry hen;
	char *dirname;
	char *path;
	int fd;

	RB_INIT(&tse_tree);

	if ((fd = open(filename, O_RDONLY)) >= 0) {
		hen = output_history(NULL, fd, NULL, NULL);
		close(fd);
		return(hen);
	}

	/*
	 * If the object does not exist acquire the history of its
	 * directory and then try accessing the object at each TID.
	 */
	if (strrchr(filename, '/')) {
		dirname = strdup(filename);
		*strrchr(dirname, '/') = 0;
	} else {
		dirname = strdup(".");
	}

	hen.tid = 0;
	hen.time32 = 0;
	if ((fd = open(dirname, O_RDONLY)) >= 0) {
		output_history(NULL, fd, NULL, &tse_tree);
		close(fd);
		free(dirname);

		tse = RB_MAX(undo_hist_entry_rb_tree, &tse_tree);
		while (tse) {
			asprintf(&path, "%s@@0x%016llx",
				 filename, tse->tse.tid);
			if ((fd = open(path, O_RDONLY)) >= 0) {
				hen = output_history(NULL, fd, NULL, NULL);
				close(fd);
				free(path);
				break;
			}
			free(path);
			tse = RB_PREV(undo_hist_entry_rb_tree, &tse_tree, tse);
		}
	}
	while ((tse = RB_ROOT(&tse_tree)) != NULL) {
		RB_REMOVE(undo_hist_entry_rb_tree, &tse_tree, tse);
		free(tse);
	}
	return(hen);
}

static
struct hammer_ioc_hist_entry
output_history(const char *filename, int fd, FILE *fp,
	       struct undo_hist_entry_rb_tree *tse_tree)
{
	struct hammer_ioc_hist_entry hen;
	struct undo_hist_entry *tse;
	char datestr[64];
	struct tm *tp;
	time_t t;
	int istmp;
	int error;

	/*
	 * Setup
	 */
	if (tse_tree == NULL) {
		tse_tree = malloc(sizeof(*tse_tree));
		RB_INIT(tse_tree);
		istmp = 1;
	} else {
		istmp = 0;
	}

	/*
	 * Collect a unique set of transaction ids
	 */
	hen = collect_history(fd, &error, tse_tree);

	if (error && filename)
		printf("%s: %s\n", filename, strerror(errno));
	else if (filename)
		printf("%s:\n", filename);

	/*
	 * If fp != NULL dump the history to stdout.
	 */
	if (fp) {
		RB_FOREACH(tse, undo_hist_entry_rb_tree, tse_tree) {
			t = (time_t)tse->tse.time32;
			tp = localtime(&t);
			strftime(datestr, sizeof(datestr),
				 "%d-%b-%Y %H:%M:%S", tp);
			printf("\t0x%016llx %s\n", tse->tse.tid, datestr);
		}
	}

	/*
	 * Caller did not want a tree returned
	 */
	if (istmp) {
		while ((tse = RB_ROOT(tse_tree)) != NULL) {
			RB_REMOVE(undo_hist_entry_rb_tree, tse_tree, tse);
			free(tse);
		}
		free(tse_tree);
	}
	return(hen);
}

static
struct hammer_ioc_hist_entry
collect_history(int fd, int *errorp, struct undo_hist_entry_rb_tree *tse_tree)
{
	struct hammer_ioc_hist_entry hen;
	struct hammer_ioc_history hist;
	struct undo_hist_entry *tse;
	int istmp;
	int i;

	/*
	 * Setup
	 */
	bzero(&hist, sizeof(hist));
	hist.beg_tid = HAMMER_MIN_TID;
	hist.end_tid = HAMMER_MAX_TID;
	hist.head.flags |= HAMMER_IOC_HISTORY_ATKEY;
	hist.key = 0;
	hist.nxt_key = HAMMER_MAX_KEY;

	hen.tid = 0;
	hen.time32 = 0;

	*errorp = 0;

	if (tse_tree == NULL) {
		tse_tree = malloc(sizeof(*tse_tree));
		RB_INIT(tse_tree);
		istmp = 1;
	} else {
		istmp = 0;
	}

	/*
	 * Collect a unique set of transaction ids
	 */
	if (ioctl(fd, HAMMERIOC_GETHISTORY, &hist) < 0) {
		*errorp = errno;
		goto done;
	}
	for (;;) {
		for (i = 0; i < hist.count; ++i) {
			tse = malloc(sizeof(*tse));
			tse->tse = hist.hist_ary[i];
			if (RB_INSERT(undo_hist_entry_rb_tree, tse_tree, tse)) {
				free(tse);
			}
		}
		if (hist.head.flags & HAMMER_IOC_HISTORY_EOF)
			break;
		if (hist.head.flags & HAMMER_IOC_HISTORY_NEXT_KEY) {
			hist.key = hist.nxt_key;
			hist.nxt_key = HAMMER_MAX_KEY;
		}
		if (hist.head.flags & HAMMER_IOC_HISTORY_NEXT_TID) 
			hist.beg_tid = hist.nxt_tid;
		if (ioctl(fd, HAMMERIOC_GETHISTORY, &hist) < 0) {
			*errorp = errno;
			break;
		}
	}

	/*
	 * Locate next-to-last entry
	 */
	tse = RB_MAX(undo_hist_entry_rb_tree, tse_tree);
	if (tse)
		tse = RB_PREV(undo_hist_entry_rb_tree, tse_tree, tse);
	if (tse)
		hen = tse->tse;

	/*
	 * Cleanup
	 */
done:
	if (istmp) {
		while ((tse = RB_ROOT(tse_tree)) != NULL) {
			RB_REMOVE(undo_hist_entry_rb_tree, tse_tree, tse);
			free(tse);
		}
		free(tse_tree);
	}
	return(hen);
}

static
hammer_tid_t
parse_delta_time(const char *timeStr)
{
	hammer_tid_t tid;

	tid = strtoull(timeStr, NULL, 0);
	return(tid);
}

static void
runcmd(int fd, const char *cmd, ...)
{
	va_list va;
	pid_t pid;
	char **av;
	int ac;
	int i;

	va_start(va, cmd);
	for (ac = 0; va_arg(va, void *) != NULL; ++ac)
		;
	va_end(va);

	av = malloc((ac + 1) * sizeof(char *));
	va_start(va, cmd);
	for (i = 0; i < ac; ++i)
		av[i] = va_arg(va, char *);
	va_end(va);
	av[i] = NULL;

	if ((pid = fork()) < 0) {
		perror("fork");
		exit(1);
	} else if (pid == 0) {
		if (fd != 1) {
			dup2(fd, 1);
			close(fd);
		}
		execv(cmd, av);
		_exit(1);
	} else {
		while (waitpid(pid, NULL, 0) != pid)
			;
	}
	free(av);
}

/*
 * Convert tid to timestamp.
 */
static char *
timestamp(hammer_ioc_hist_entry_t hen)
{
	static char timebuf[64];
	time_t t = (time_t)hen->time32;
	struct tm *tp;

	tp = localtime(&t);
	strftime(timebuf, sizeof(timebuf), "%d-%b-%Y %H:%M:%S", tp);
	return(timebuf);
}

static
int
undo_hist_entry_compare(struct undo_hist_entry *he1,
			struct undo_hist_entry *he2)
{
	if (he1->tse.tid < he2->tse.tid)
		return(-1);
	if (he1->tse.tid > he2->tse.tid)
		return(1);
	return(0);
}

static void
usage(void)
{
	fprintf(stderr, "undo [-adDiuv] [-o outfile] "
			"[-t transaction-id] [-t transaction-id] path...\n"
			"    -a       Iterate all historical segments\n"
			"    -d       Forward diff\n"
			"    -D       Reverse diff\n"
			"    -i       Dump history transaction ids\n"
			"    -u       Generate .undo files\n"
			"    -v       Verbose\n"
			"    -o file  Output to the specified file\n"
			"    -t TID   Retrieve as of transaction-id, TID\n"
			"             (a second `-t TID' to diff two versions)\n");
	exit(1);
}

