/*
 * Copyright (c) 1980, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * @(#) Copyright (c) 1980, 1993 The Regents of the University of California.  All rights reserved.
 * @(#)main.c	8.1 (Berkeley) 6/6/93
 * $FreeBSD: src/usr.sbin/config/main.c,v 1.37.2.3 2001/06/13 00:25:53 cg Exp $
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <dirent.h>
#include <sysexits.h>
#include <unistd.h>
#include "y.tab.h"
#include "config.h"

#ifndef TRUE
#define TRUE	(1)
#endif

#ifndef FALSE
#define FALSE	(0)
#endif

#define	CDIR	"../compile/"

char *	platformname;
char *	machinename;
char *	machinearchname;

struct cputype	*cputype;
struct opt	*opt, *mkopt;
struct opt_list	*otab;

char *	PREFIX;
char 	destdir[MAXPATHLEN];
char 	srcdir[MAXPATHLEN - 32];

static int no_config_clobber = TRUE;
int	debugging;

extern int yyparse(void);
static void configfile(void);
static void get_srcdir(void);
static void usage(void);

/*
 * Config builds a set of files for building a UNIX
 * system given a description of the desired system.
 */
int
main(int argc, char *argv[])
{
	struct stat buf;
	int ch, len;
	char *p;
	char linkdest[MAXPATHLEN];
#if 0
	unsigned int i;
	char linksrc[64];
	static const char *emus[] = { "linux" };
#endif

	while ((ch = getopt(argc, argv, "d:gr")) != -1)
		switch (ch) {
		case 'd':
			if (*destdir == '\0')
				strlcpy(destdir, optarg, sizeof(destdir));
			else
				errx(2, "directory already set");
			break;
		case 'g':
			debugging++;
			break;
		case 'r':
			no_config_clobber = FALSE;
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();

	if (freopen(PREFIX = *argv, "r", stdin) == NULL)
		err(2, "%s", PREFIX);

	if (*destdir != '\0') {
		len = strlen(destdir);
		while (len > 1 && destdir[len - 1] == '/')
			destdir[--len] = '\0';
		get_srcdir();
	} else {
		strlcpy(destdir, CDIR, sizeof(destdir));
		strlcat(destdir, PREFIX, sizeof(destdir));
	}

	p = path(NULL);
	if (stat(p, &buf)) {
		if (mkdir(p, 0777))
			err(2, "%s", p);
	}
	else if ((buf.st_mode & S_IFMT) != S_IFDIR) {
		errx(2, "%s isn't a directory", p);
	}
	else if (!no_config_clobber) {
		char tmp[strlen(p) + 8];

		fprintf(stderr, "Removing old directory %s:  ", p);
		fflush(stderr);
		snprintf(tmp, sizeof(tmp), "rm -rf %s", p);
		if (system(tmp)) {
			fprintf(stderr, "Failed!\n");
			err(2, "%s", tmp);
		}
		fprintf(stderr, "Done.\n");
		if (mkdir(p, 0777))
			err(2, "%s", p);
	}

	dtab = NULL;
	if (yyparse())
		exit(3);
	if (platformname == NULL) {
		printf("Specify platform architecture, e.g. 'platform pc64'\n");
		exit(1);
	}
	if (machinename == NULL) {
		printf("Specify machine architecture, e.g. 'machine x86_64'\n");
		exit(1);
	}
	if (machinearchname == NULL) {
		printf("Specify cpu architecture, e.g. 'machine_arch x86_64'\n");
		exit(1);
	}
	newbus_ioconf();
	
	/*
	 * "machine" points into platform/<PLATFORM>/include
	 */
	if (*srcdir == '\0')
		snprintf(linkdest, sizeof(linkdest), "../../platform/%s/include",
		    platformname);
	else
		snprintf(linkdest, sizeof(linkdest), "%s/platform/%s/include",
		    srcdir, platformname);
	symlink(linkdest, path("machine"));

	/*
	 * "machine_base" points into platform/<PLATFORM>
	 */
	if (*srcdir == '\0')
		snprintf(linkdest, sizeof(linkdest), "../../platform/%s",
		    platformname);
	else
		snprintf(linkdest, sizeof(linkdest), "%s/platform/%s",
		    srcdir, platformname);
	symlink(linkdest, path("machine_base"));

	/*
	 * "cpu" points to cpu/<MACHINE_ARCH>/include
	 */
	if (*srcdir == '\0')
		snprintf(linkdest, sizeof(linkdest),
			 "../../cpu/%s/include", machinearchname);
	else
		snprintf(linkdest, sizeof(linkdest),
			 "%s/cpu/%s/include", srcdir, machinearchname);
	symlink(linkdest, path("cpu"));

	/*
	 * "cpu_base" points to cpu/<MACHINE_ARCH>
	 */
	if (*srcdir == '\0')
		snprintf(linkdest, sizeof(linkdest), "../../cpu/%s",
		    machinearchname);
	else
		snprintf(linkdest, sizeof(linkdest), "%s/cpu/%s",
		    srcdir, machinearchname);
	symlink(linkdest, path("cpu_base"));

	/*
	 * XXX check directory structure for architecture subdirectories and
	 * create the symlinks automatically XXX
	 */
#if 0
	for (i = 0; i < NELEM(emus); ++i) {
		if (*srcdir == 0)  {
			snprintf(linkdest, sizeof(linkdest),
			    "../../emulation/%s/%s",
			    emus[i], machinearchname);
		} else {
			snprintf(linkdest, sizeof(linkdest),
			    "%s/emulation/%s/%s",
			    srcdir, emus[i], machinearchname);
		}
		snprintf(linksrc, sizeof(linksrc), "arch_%s", emus[i]);
		symlink(linkdest, path(linksrc));
	}
#endif

	options();			/* make options .h files */
	makefile();			/* build Makefile */
	headers();			/* make a lot of .h files */
	configfile();			/* put config file into kernel*/
	printf("Kernel build directory is %s\n", p);
	exit(EX_OK);
}

/*
 * get_srcdir
 *	determine the root of the kernel source tree
 *	and save that in srcdir.
 */
static void
get_srcdir(void)
{
	
	if (realpath("..", srcdir) == NULL)
		errx(2, "Unable to find root of source tree");
}

static void
usage(void)
{
	
	fprintf(stderr, "usage: config [-gpr] [-d destdir] sysname\n");
	exit(1);
}

/*
 * get_word
 *	returns EOF on end of file
 *	NULL on end of line
 *	pointer to the word otherwise
 */
char *
get_word(FILE *fp)
{
	static char line[80];
	int ch;
	char *cp;
	int escaped_nl = 0;

begin:
	while ((ch = getc(fp)) != EOF)
		if (ch != ' ' && ch != '\t')
			break;
	if (ch == EOF)
		return((char *)EOF);
	if (ch == '\\') {
		escaped_nl = 1;
		goto begin;
	}
	if (ch == '\n') {
		if (escaped_nl) {
			escaped_nl = 0;
			goto begin;
		}
		else
			return(NULL);
	}
	cp = line;
	*cp++ = ch;
	while ((ch = getc(fp)) != EOF) {
		if (isspace(ch))
			break;
		*cp++ = ch;
	}
	*cp = 0;
	if (ch == EOF)
		return((char *)EOF);
	ungetc(ch, fp);
	return(line);
}

/*
 * get_quoted_word
 *	like get_word but will accept something in double or single quotes
 *	(to allow embedded spaces).
 */
char *
get_quoted_word(FILE *fp)
{
	static char line[256];
	int ch;
	char *cp;
	int escaped_nl = 0;

begin:
	while ((ch = getc(fp)) != EOF)
		if (ch != ' ' && ch != '\t')
			break;
	if (ch == EOF)
		return((char *)EOF);
	if (ch == '\\') {
		escaped_nl = 1;
		goto begin;
	}
	if (ch == '\n') {
		if (escaped_nl) {
			escaped_nl = 0;
			goto begin;
		}
		else
			return(NULL);
	}
	cp = line;
	if (ch == '"' || ch == '\'') {
		int quote = ch;

		while ((ch = getc(fp)) != EOF) {
			if (ch == quote)
				break;
			if (ch == '\n') {
				*cp = 0;
				printf("config: missing quote reading `%s'\n",
					line);
				exit(2);
			}
			*cp++ = ch;
		}
	} else {
		*cp++ = ch;
		while ((ch = getc(fp)) != EOF) {
			if (isspace(ch))
				break;
			*cp++ = ch;
		}
		if (ch != EOF)
			ungetc(ch, fp);
	}
	*cp = 0;
	if (ch == EOF)
		return((char *)EOF);
	return(line);
}

/*
 * prepend the path to a filename
 */
char *
path(const char *file)
{
	char *cp;

	cp = malloc((size_t)(strlen(destdir) + (file ? strlen(file) : 0) + 2));
	strcpy(cp, destdir);
	if (file != NULL) {
		strcat(cp, "/");
		strcat(cp, file);
	}
	return(cp);
}

static void
configfile(void)
{
	FILE *fi, *fo;
	char *p;
	int i;
	
	fi = fopen(PREFIX, "r");
	if (fi == NULL)
		err(2, "%s", PREFIX);
	fo = fopen(p = path("config.c.new"), "w");
	if (fo == NULL)
		err(2, "%s", p);
	fprintf(fo, "#include \"opt_config.h\"\n");
	fprintf(fo, "#ifdef INCLUDE_CONFIG_FILE \n");
	fprintf(fo, "/* Mark config as used, so gcc doesn't optimize it away. */\n");
	fprintf(fo, "#include <sys/types.h>\n");
	fprintf(fo, "__used\n");
	fprintf(fo, "static const char config[] = \"\\\n");
	fprintf(fo, "START CONFIG FILE %s\\n\\\n___", PREFIX);
	while (EOF != (i = getc(fi))) {
		if (i == '\n') {
			fprintf(fo, "\\n\\\n___");
		} else if (i == '\"') {
			fprintf(fo, "\\\"");
		} else if (i == '\\') {
			fprintf(fo, "\\\\");
		} else {
			putc(i, fo);
		}
	}
	fprintf(fo, "\\n\\\nEND CONFIG FILE %s\\n\\\n", PREFIX);
	fprintf(fo, "\";\n");
	fprintf(fo, "\n#endif /* INCLUDE_CONFIG_FILE */\n");
	fclose(fi);
	fclose(fo);
	moveifchanged(path("config.c.new"), path("config.c"));
}

/*
 * moveifchanged --
 *	compare two files; rename if changed.
 */
void
moveifchanged(const char *from_name, const char *to_name)
{
	char *p, *q;
	int changed;
	size_t tsize;
	struct stat from_sb, to_sb;
	int from_fd, to_fd;

	changed = 0;

	if ((from_fd = open(from_name, O_RDONLY)) < 0)
		err(EX_OSERR, "moveifchanged open(%s)", from_name);

	if ((to_fd = open(to_name, O_RDONLY)) < 0)
		changed++;

	if (!changed && fstat(from_fd, &from_sb) < 0)
		err(EX_OSERR, "moveifchanged fstat(%s)", from_name);

	if (!changed && fstat(to_fd, &to_sb) < 0)
		err(EX_OSERR, "moveifchanged fstat(%s)", to_name);

	if (!changed && from_sb.st_size != to_sb.st_size)
		changed++;

	tsize = (size_t)from_sb.st_size;

	if (!changed) {
		p = mmap(NULL, tsize, PROT_READ, MAP_SHARED, from_fd, (off_t)0);
#ifndef MAP_FAILED
#define MAP_FAILED ((caddr_t)-1)
#endif
		if (p == MAP_FAILED)
			err(EX_OSERR, "mmap %s", from_name);
		q = mmap(NULL, tsize, PROT_READ, MAP_SHARED, to_fd, (off_t)0);
		if (q == MAP_FAILED)
			err(EX_OSERR, "mmap %s", to_name);

		changed = memcmp(p, q, tsize);
		munmap(p, tsize);
		munmap(q, tsize);
	}
	if (changed) {
		if (rename(from_name, to_name) < 0)
			err(EX_OSERR, "rename(%s, %s)", from_name, to_name);
	} else {
		if (unlink(from_name) < 0)
			err(EX_OSERR, "unlink(%s)", from_name);
	}
}
