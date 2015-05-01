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
	ino_t inum;
};

enum undo_type { TYPE_FILE, TYPE_DIFF, TYPE_RDIFF, TYPE_HISTORY };
enum undo_cmd { CMD_DUMP, CMD_ITERATEALL };

#define UNDO_FLAG_MULT		0x0001
#define UNDO_FLAG_INOCHG	0x0002
#define UNDO_FLAG_SETTID1	0x0004
#define UNDO_FLAG_SETTID2	0x0008

static int undo_hist_entry_compare(struct undo_hist_entry *he1,
		    struct undo_hist_entry *he2);
static void doiterate(const char *filename, const char *outFileName,
		   const char *outFilePostfix, int flags,
		   struct hammer_ioc_hist_entry ts1,
		   struct hammer_ioc_hist_entry ts2,
		   enum undo_cmd cmd, enum undo_type type);
static void dogenerate(const char *filename, const char *outFileName,
		   const char *outFilePostfix,
		   int flags, int idx, enum undo_type type,
		   struct hammer_ioc_hist_entry ts1,
		   struct hammer_ioc_hist_entry ts2);
static void collect_history(int fd, int *error,
		   struct undo_hist_entry_rb_tree *tse_tree);
static void collect_dir_history(const char *filename, int *error,
		   struct undo_hist_entry_rb_tree *dir_tree);
static void clean_tree(struct undo_hist_entry_rb_tree *tree);
static hammer_tid_t parse_delta_time(const char *timeStr, int *flags,
		   int ind_flag);
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
	enum undo_cmd cmd;
	enum undo_type type;
	struct hammer_ioc_hist_entry ts1;
	struct hammer_ioc_hist_entry ts2;
	int c;
	int count_t;
	int flags;

	bzero(&ts1, sizeof(ts1));
	bzero(&ts2, sizeof(ts2));

	cmd = CMD_DUMP;
	type = TYPE_FILE;
	count_t = 0;
	flags = 0;

	while ((c = getopt(ac, av, "adDiuvo:t:")) != -1) {
		switch(c) {
		case 'd':
			type = TYPE_DIFF;
			break;
		case 'D':
			type = TYPE_RDIFF;
			break;
		case 'i':
			if (type != TYPE_FILE)
				usage();
			type = TYPE_HISTORY;
			cmd = CMD_ITERATEALL;
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
			/*
			 * Parse one or two -t options.  If two are specified
			 * -d is implied (but may be overridden)
			 */
			++count_t;
			if (count_t == 1) {
				ts1.tid = parse_delta_time(optarg, &flags,
				                           UNDO_FLAG_SETTID1);
			} else if (count_t == 2) {
				ts2.tid = parse_delta_time(optarg, &flags,
				                           UNDO_FLAG_SETTID2);
				if (type == TYPE_FILE)
					type = TYPE_DIFF;
			} else {
				usage();
			}
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
	if (ac > 1)
		flags |= UNDO_FLAG_MULT;

	if (ac == 0)
		usage();

	/*
	 * Validate the output template, if specified.
	 */
	if (outFileName && (flags & UNDO_FLAG_MULT)) {
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
		doiterate(*av, outFileName, outFilePostfix,
			  flags, ts1, ts2, cmd, type);
		++av;
		--ac;
	}
	return(0);
}

/*
 * Iterate through a file's history.  If cmd == CMD_DUMP we take the
 * next-to-last transaction id, unless another given.  Otherwise if
 * cmd == CMD_ITERATEALL we scan all transaction ids.
 *
 * Also iterate through the directory's history to locate other inodes that
 * used the particular file name.
 */
static
void
doiterate(const char *filename, const char *outFileName,
	  const char *outFilePostfix, int flags,
	  struct hammer_ioc_hist_entry ts1,
	  struct hammer_ioc_hist_entry ts2,
	  enum undo_cmd cmd, enum undo_type type)
{
	struct undo_hist_entry_rb_tree dir_tree;
	struct undo_hist_entry_rb_tree tse_tree;
	struct undo_hist_entry *tse1;
	struct undo_hist_entry *tse2;
	struct hammer_ioc_hist_entry tid_max;
	struct stat sb;
	char *path = NULL;
	int i;
	int fd;
	int error;

	RB_INIT(&dir_tree);
	RB_INIT(&tse_tree);

	tid_max.tid = HAMMER_MAX_TID;
	tid_max.time32 = 0;

	/*
	 * Use the directory history to locate all possible versions of
	 * the file.
	 */
	collect_dir_history(filename, &error, &dir_tree);
	RB_FOREACH(tse1, undo_hist_entry_rb_tree, &dir_tree) {
		asprintf(&path, "%s@@0x%016jx", filename, (uintmax_t)tse1->tse.tid);
		if (stat(path, &sb) == 0 && sb.st_mode & S_IFIFO) {
			fprintf(stderr, "Warning: fake transaction id 0x%016jx\n", (uintmax_t)tse1->tse.tid);
			continue;
		}
		if ((fd = open(path, O_RDONLY)) > 0) {
			collect_history(fd, &error, &tse_tree);
			close(fd);
		}
		free(path);
	}
	if ((fd = open(filename, O_RDONLY)) > 0) {
		collect_history(fd, &error, &tse_tree);
		close(fd);
	}
	if (cmd == CMD_DUMP) {
		if ((ts1.tid == 0 ||
		     flags & (UNDO_FLAG_SETTID1|UNDO_FLAG_SETTID2)) &&
		    RB_EMPTY(&tse_tree)) {
			if ((fd = open(filename, O_RDONLY)) > 0) {
				collect_history(fd, &error, &tse_tree);
				close(fd);
			}
		}
		/*
		 * Find entry if tid set to placeholder index
		 */
		if (flags & UNDO_FLAG_SETTID1){
			tse1 = RB_MAX(undo_hist_entry_rb_tree, &tse_tree);
			while (tse1 && ts1.tid--) {
				tse1 = RB_PREV(undo_hist_entry_rb_tree,
					       &tse_tree, tse1);
			}
			if (tse1)
				ts1 = tse1->tse;
			else
				ts1.tid = 0;
		}
		if (flags & UNDO_FLAG_SETTID2){
			tse2 = RB_MAX(undo_hist_entry_rb_tree, &tse_tree);
			while (tse2 && ts2.tid--) {
				tse2 = RB_PREV(undo_hist_entry_rb_tree,
					       &tse_tree, tse2);
			}
			if (tse2)
				ts2 = tse2->tse;
			else
				ts2.tid = 0;
		}

		/*
		 * Single entry, most recent prior to current
		 */
		if (ts1.tid == 0) {
			tse2 = RB_MAX(undo_hist_entry_rb_tree, &tse_tree);
			if (tse2) {
				ts2 = tse2->tse;
				tse1 = RB_PREV(undo_hist_entry_rb_tree,
					       &tse_tree, tse2);
				if (tse1)
					ts1 = tse1->tse;
			}
		}
		if (ts1.tid == 0) {
			printf("%s: No UNDO history found\n", filename);
		} else {
			dogenerate(filename,
				   outFileName, outFilePostfix,
				   0, 0, type,
				   ts1, ts2);
		}
	} else if (RB_ROOT(&tse_tree)) {
		/*
		 * Iterate entire history
		 */
		printf("%s: ITERATE ENTIRE HISTORY\n", filename);

		tse1 = NULL;
		i = 0;
		RB_FOREACH(tse2, undo_hist_entry_rb_tree, &tse_tree) {
			if (tse1) {
				dogenerate(filename,
					   outFileName, outFilePostfix,
					   flags, i, type,
					   tse1->tse, tse2->tse);
			}
			if (tse1 && tse2->inum != tse1->inum)
				flags |= UNDO_FLAG_INOCHG;
			else
				flags &= ~UNDO_FLAG_INOCHG;
			tse1 = tse2;
			++i;
		}
		/*
		 * There is no delta to print for the last pair,
		 * because they are identical.
		 */
		if (type != TYPE_DIFF && type != TYPE_RDIFF) {
			dogenerate(filename,
				   outFileName, outFilePostfix,
				   flags, i, type,
				   tse1->tse, tid_max);
		}
	} else {
		printf("%s: ITERATE ENTIRE HISTORY: %s\n",
		       filename, strerror(error));
	}
	clean_tree(&dir_tree);
	clean_tree(&tse_tree);
}

/*
 * Generate output for a file as-of ts1 (ts1 may be 0!), if diffing then
 * through ts2.
 */
static
void
dogenerate(const char *filename, const char *outFileName,
	   const char *outFilePostfix,
	   int flags, int idx, enum undo_type type,
	   struct hammer_ioc_hist_entry ts1,
	   struct hammer_ioc_hist_entry ts2)
{
	struct stat st;
	const char *elm;
	char *ipath1 = NULL;
	char *ipath2 = NULL;
	FILE *fi;
	FILE *fp; 
	char *buf;
	char *path;
	time_t t;
	struct tm *tp;
	char datestr[64];
	int n;

	buf = malloc(8192);

	/*
	 * Open the input file.  If ts1 is 0 try to locate the most recent
	 * version of the file prior to the current version.
	 */
	if (ts1.tid == 0)
		asprintf(&ipath1, "%s", filename);
	else
		asprintf(&ipath1, "%s@@0x%016jx", filename, (uintmax_t)ts1.tid);

	if (ts2.tid == 0)
		asprintf(&ipath2, "%s", filename);
	else
		asprintf(&ipath2, "%s@@0x%016jx", filename, (uintmax_t)ts2.tid);

	if (lstat(ipath1, &st) < 0 && lstat(ipath2, &st) < 0) {
		if (idx == 0 || VerboseOpt) {
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
		if (flags & UNDO_FLAG_MULT) {
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
		if ((flags & UNDO_FLAG_MULT) && type == TYPE_FILE) {
			if (idx >= 0) {
				printf("\n>>> %s %04d 0x%016jx %s\n\n",
				       filename, idx, (uintmax_t)ts1.tid,
				       timestamp(&ts1));
			} else {
				printf("\n>>> %s ---- 0x%016jx %s\n\n",
				       filename, (uintmax_t)ts1.tid,
				       timestamp(&ts1));
			}
		} else if (idx >= 0 && type == TYPE_FILE) {
			printf("\n>>> %s %04d 0x%016jx %s\n\n",
			       filename, idx, (uintmax_t)ts1.tid,
			       timestamp(&ts1));
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
		t = (time_t)ts1.time32;
		tp = localtime(&t);
		strftime(datestr, sizeof(datestr), "%d-%b-%Y %H:%M:%S", tp);
		printf("\t0x%016jx %s", (uintmax_t)ts1.tid, datestr);
		if (flags & UNDO_FLAG_INOCHG)
			printf(" inode-change");
		if (lstat(ipath1, &st) < 0)
			printf(" file-deleted");
		printf("\n");
		break;
	}

	if (fp != stdout)
		fclose(fp);
done:
	free(buf);
}

static
void
clean_tree(struct undo_hist_entry_rb_tree *tree)
{
	struct undo_hist_entry *tse;

	while ((tse = RB_ROOT(tree)) != NULL) {
		RB_REMOVE(undo_hist_entry_rb_tree, tree, tse);
		free(tse);
	}
}

static
void
collect_history(int fd, int *errorp, struct undo_hist_entry_rb_tree *tse_tree)
{
	struct hammer_ioc_history hist;
	struct undo_hist_entry *tse;
	struct stat st;
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

	*errorp = 0;

	if (tse_tree == NULL) {
		tse_tree = malloc(sizeof(*tse_tree));
		RB_INIT(tse_tree);
		istmp = 1;
	} else {
		istmp = 0;
	}

	/*
	 * Save the inode so inode changes can be reported.
	 */
	st.st_ino = 0;
	fstat(fd, &st);

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
			tse->inum = st.st_ino;
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
	 * Cleanup
	 */
done:
	if (istmp) {
		clean_tree(tse_tree);
		free(tse_tree);
	}
}

static
void
collect_dir_history(const char *filename, int *errorp,
		    struct undo_hist_entry_rb_tree *dir_tree)
{
	char *dirname;
	int fd;
	int error;

	*errorp = 0;
	if (strrchr(filename, '/')) {
		dirname = strdup(filename);
		*strrchr(dirname, '/') = 0;
	} else {
		dirname = strdup(".");
	}
	if ((fd = open(dirname, O_RDONLY)) > 0) {
		collect_history(fd, &error, dir_tree);
		close(fd);
	}
}

static
hammer_tid_t
parse_delta_time(const char *timeStr, int *flags, int ind_flag)
{
	hammer_tid_t tid;

	tid = strtoull(timeStr, NULL, 0);
	if (timeStr[0] == '+')
		++timeStr;
	if (timeStr[0] >= '0' && timeStr[0] <= '9' && timeStr[1] != 'x')
		*flags |= ind_flag;
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
			"             (a second `-t TID' to diff two)\n"
			"    transaction ids must be prefixed with 0x, and\n"
			"    otherwise may specify an index starting at 0\n"
			"    and iterating backwards through the history.\n"
	);
	exit(1);
}

