/*
 * Copyright (c) 1993, 19801990
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 * @(#)mkmakefile.c	8.1 (Berkeley) 6/6/93
 * $FreeBSD: src/usr.sbin/config/mkmakefile.c,v 1.51.2.3 2001/01/23 00:09:32 peter Exp $
 */

/*
 * Build the makefile for the system, from
 * the information in the 'files' files and the
 * additional files for the machine being compiled to.
 */

#include <ctype.h>
#include <err.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include "y.tab.h"
#include "config.h"
#include "configvers.h"

#define next_word(fp, wd)						\
	{								\
		char *word;						\
									\
		word = get_word(fp);					\
		if (word == (char *)EOF)				\
			return;						\
		else							\
			wd = word;					\
	}
#define next_quoted_word(fp, wd)					\
	{								\
		char *word;						\
									\
		word = get_quoted_word(fp);				\
		if (word == (char *)EOF)				\
			return;						\
		else							\
			wd = word;					\
	}

static struct file_list *fcur;

static char *tail(char *);
static void do_clean(FILE *);
static void do_rules(FILE *);
static void do_sfiles(FILE *);
static void do_mfiles(FILE *);
static void do_cfiles(FILE *);
static void do_objs(FILE *);
static void do_before_depend(FILE *);
static int opteq(char *, char *);
static void read_files(void);

/*
 * Lookup a file, by name.
 */
static struct file_list *
fl_lookup(char *file)
{
	struct file_list *fp;

	for (fp = ftab; fp != NULL; fp = fp->f_next) {
		if (strcmp(fp->f_fn, file) == 0)
			return(fp);
	}
	return(0);
}

/*
 * Lookup a file, by final component name.
 */
static struct file_list *
fltail_lookup(char *file)
{
	struct file_list *fp;

	for (fp = ftab; fp != NULL; fp = fp->f_next) {
		if (strcmp(tail(fp->f_fn), tail(file)) == 0)
			return(fp);
	}
	return(0);
}

/*
 * Make a new file list entry
 */
static struct file_list *
new_fent(void)
{
	struct file_list *fp;

	fp = malloc(sizeof(*fp));
	bzero(fp, sizeof(*fp));
	if (fcur == NULL)
		fcur = ftab = fp;
	else
		fcur->f_next = fp;
	fcur = fp;
	return(fp);
}

/*
 * Build the makefile from the skeleton
 */
void
makefile(void)
{
	FILE *ifp, *ofp;
	char line[BUFSIZ];
	struct opt *op;
	int versreq;

	read_files();
	snprintf(line, sizeof(line), "../platform/%s/conf/Makefile",
		 platformname);
	ifp = fopen(line, "r");
	if (ifp == NULL) {
		snprintf(line, sizeof(line), "Makefile.%s", platformname);
		ifp = fopen(line, "r");
	}
	if (ifp == NULL)
		err(1, "%s", line);
	ofp = fopen(path("Makefile.new"), "w");
	if (ofp == NULL)
		err(1, "%s", path("Makefile.new"));
	fprintf(ofp, "KERN_IDENT=%s\n", raisestr(ident));
	fprintf(ofp, "MACHINE_PLATFORM=%s\n", platformname);
	fprintf(ofp, "MACHINE=%s\n", machinename);
	fprintf(ofp, "MACHINE_ARCH=%s\n", machinearchname);
	fprintf(ofp, ".makeenv MACHINE_PLATFORM\n");
	fprintf(ofp, ".makeenv MACHINE\n");
	fprintf(ofp, ".makeenv MACHINE_ARCH\n");
	fprintf(ofp, "IDENT=");
	if (profiling) {
		/* Don't compile kernel profiling code for vkernel */
		if (strncmp(platformname, "vkernel", 6) != 0)
			fprintf(ofp, " -DGPROF");
	}

	if (cputype == 0) {
		printf("cpu type must be specified\n");
		exit(1);
	}
	fprintf(ofp, "\n");
	for (op = mkopt; op != NULL; op = op->op_next)
		fprintf(ofp, "%s=%s\n", op->op_name, op->op_value);
	if (debugging)
		fprintf(ofp, "DEBUG=-g\n");
	if (profiling) {
		fprintf(ofp, "PROF=-pg\n");
		fprintf(ofp, "PROFLEVEL=%d\n", profiling);
	}
	if (*srcdir != '\0')
		fprintf(ofp,"S=%s\n", srcdir);
	while (fgets(line, BUFSIZ, ifp) != 0) {
		if (*line != '%') {
			fprintf(ofp, "%s", line);
			continue;
		}
		if (strcmp(line, "%BEFORE_DEPEND\n") == 0)
			do_before_depend(ofp);
		else if (strcmp(line, "%OBJS\n") == 0)
			do_objs(ofp);
		else if (strcmp(line, "%MFILES\n") == 0)
			do_mfiles(ofp);
		else if (strcmp(line, "%CFILES\n") == 0)
			do_cfiles(ofp);
		else if (strcmp(line, "%SFILES\n") == 0)
			do_sfiles(ofp);
		else if (strcmp(line, "%RULES\n") == 0)
			do_rules(ofp);
		else if (strcmp(line, "%CLEAN\n") == 0)
			do_clean(ofp);
		else if (strncmp(line, "%VERSREQ=", sizeof("%VERSREQ=") - 1) == 0) {
			versreq = atoi(line + sizeof("%VERSREQ=") - 1);
			if (versreq != CONFIGVERS) {
				fprintf(stderr, "ERROR: version of config(8) does not match kernel!\n");
				fprintf(stderr, "config version = %d, ", CONFIGVERS);
				fprintf(stderr, "version required = %d\n\n", versreq);
				fprintf(stderr, "Make sure that /usr/src/usr.sbin/config is in sync\n");
				fprintf(stderr, "with your /usr/src/sys and install a new config binary\n");
				fprintf(stderr, "before trying this again.\n\n");
				fprintf(stderr, "If running the new config fails check your config\n");
				fprintf(stderr, "file against the GENERIC or LINT config files for\n");
				fprintf(stderr, "changes in config syntax, or option/device naming\n");
				fprintf(stderr, "conventions\n\n");
				exit(1);
			}
		} else
			fprintf(stderr,
			    "Unknown %% construct in generic makefile: %s",
			    line);
	}
	fclose(ifp);
	fclose(ofp);
	moveifchanged(path("Makefile.new"), path("Makefile"));
}

/*
 * Read in the information about files used in making the system.
 * Store it in the ftab linked list.
 */
static void
read_files(void)
{
	FILE *fp;
	struct file_list *tp, *pf;
	struct device *dp;
	struct device *save_dp;
	struct opt *op;
	char *wd, *this, *needs, *special, *depends, *clean, *warning;
	char fname[MAXPATHLEN];
	int nonoptional;
	int nreqs, first = 1, configdep, isdup, std, filetype,
	    imp_rule, no_obj, before_depend, nowerror, mandatory;

	ftab = 0;
	save_dp = NULL;
	if (ident == NULL) {
		printf("no ident line specified\n");
		exit(1);
	}
	snprintf(fname, sizeof(fname), "../conf/files");
openit:
	fp = fopen(fname, "r");
	if (fp == NULL)
		err(1, "%s", fname);
next:
	/*
	 * filename    [ standard | mandatory | optional ] [ config-dependent ]
	 *	[ dev* | profiling-routine ] [ no-obj ]
	 *	[ compile-with "compile rule" [no-implicit-rule] ]
	 *      [ dependency "dependency-list"] [ before-depend ]
	 *	[ clean "file-list"] [ warning "text warning" ]
	 */
	wd = get_word(fp);
	if (wd == (char *)EOF) {
		fclose(fp);
		if (first == 1) {
			first++;
			snprintf(fname, sizeof(fname),
			    "../platform/%s/conf/files",
			    platformname);
			fp = fopen(fname, "r");
			if (fp != NULL)
				goto next;
			snprintf(fname, sizeof(fname),
			    "files.%s", platformname);
			goto openit;
		}
		if (first == 2) {
			first++;
			snprintf(fname, sizeof(fname),
			    "files.%s", raisestr(ident));
			fp = fopen(fname, "r");
			if (fp != NULL)
				goto next;
		}
		return;
	}
	if (wd == NULL)
		goto next;
	if (wd[0] == '#')
	{
		while (((wd = get_word(fp)) != (char *)EOF) && wd)
			;
		goto next;
	}
	this = strdup(wd);
	next_word(fp, wd);
	if (wd == NULL) {
		printf("%s: No type for %s.\n",
		    fname, this);
		exit(1);
	}
	if ((pf = fl_lookup(this)) && (pf->f_type != INVISIBLE || pf->f_flags))
		isdup = 1;
	else
		isdup = 0;
	tp = NULL;
	if (first == 3 && pf == NULL && (tp = fltail_lookup(this)) != NULL) {
		if (tp->f_type != INVISIBLE || tp->f_flags)
			printf("%s: Local file %s overrides %s.\n",
			    fname, this, tp->f_fn);
		else
			printf("%s: Local file %s could override %s"
			    " with a different kernel configuration.\n",
			    fname, this, tp->f_fn);
	}
	nreqs = 0;
	special = NULL;
	depends = NULL;
	clean = NULL;
	warning = NULL;
	configdep = 0;
	needs = NULL;
	std = mandatory = nonoptional = 0;
	imp_rule = 0;
	no_obj = 0;
	before_depend = 0;
	nowerror = 0;
	filetype = NORMAL;
	if (strcmp(wd, "standard") == 0) {
		std = 1;
	} else if (strcmp(wd, "mandatory") == 0) {
		/*
		 * If an entry is marked "mandatory", config will abort if 
		 * it's not called by a configuration line in the config
		 * file.  Apart from this, the device is handled like one
		 * marked "optional".
		 */
		mandatory = 1;
	} else if (strcmp(wd, "nonoptional") == 0) {
		nonoptional = 1;
	} else if (strcmp(wd, "optional") == 0) {
		/* don't need to do anything */
	} else {
		printf("%s: %s must be optional, mandatory or standard\n",
		       fname, this);
		printf("Alternatively, your version of config(8) may be out of sync with your\nkernel source.\n");
		exit(1);
	}
nextparam:
	next_word(fp, wd);
	if (wd == NULL)
		goto doneparam;
	if (strcmp(wd, "config-dependent") == 0) {
		configdep++;
		goto nextparam;
	}
	if (strcmp(wd, "no-obj") == 0) {
		no_obj++;
		goto nextparam;
	}
	if (strcmp(wd, "no-implicit-rule") == 0) {
		if (special == NULL) {
			printf("%s: alternate rule required when "
			       "\"no-implicit-rule\" is specified.\n",
			       fname);
		}
		imp_rule++;
		goto nextparam;
	}
	if (strcmp(wd, "before-depend") == 0) {
		before_depend++;
		goto nextparam;
	}
	if (strcmp(wd, "dependency") == 0) {
		next_quoted_word(fp, wd);
		if (wd == NULL) {
			printf("%s: %s missing compile command string.\n",
			       fname, this);
			exit(1);
		}
		depends = strdup(wd);
		goto nextparam;
	}
	if (strcmp(wd, "clean") == 0) {
		next_quoted_word(fp, wd);
		if (wd == NULL) {
			printf("%s: %s missing clean file list.\n",
			       fname, this);
			exit(1);
		}
		clean = strdup(wd);
		goto nextparam;
	}
	if (strcmp(wd, "compile-with") == 0) {
		next_quoted_word(fp, wd);
		if (wd == NULL) {
			printf("%s: %s missing compile command string.\n",
			       fname, this);
			exit(1);
		}
		special = strdup(wd);
		goto nextparam;
	}
	if (strcmp(wd, "nowerror") == 0) {
		nowerror++;
		goto nextparam;
	}
	if (strcmp(wd, "warning") == 0) {
		next_quoted_word(fp, wd);
		if (wd == NULL) {
			printf("%s: %s missing warning text string.\n",
				fname, this);
			exit(1);
		}
		warning = strdup(wd);
		goto nextparam;
	}
	nreqs++;
	if (strcmp(wd, "local") == 0) {
		filetype = LOCAL;
		goto nextparam;
	}
	if (strcmp(wd, "no-depend") == 0) {
		filetype = NODEPEND;
		goto nextparam;
	}
	if (strcmp(wd, "device-driver") == 0) {
		printf("%s: `device-driver' flag obsolete.\n", fname);
		exit(1);
	}
	if (strcmp(wd, "profiling-routine") == 0) {
		filetype = PROFILING;
		goto nextparam;
	}
	if (needs == NULL && nreqs == 1)
		needs = strdup(wd);
	if (isdup)
		goto invis;
	for (dp = dtab; dp != NULL; save_dp = dp, dp = dp->d_next)
		if (strcmp(dp->d_name, wd) == 0) {
			if (std && dp->d_type == PSEUDO_DEVICE &&
			    dp->d_count <= 0)
				dp->d_count = 1;
			goto nextparam;
		}
	if (mandatory) {
		printf("%s: mandatory device \"%s\" not found\n",
		       fname, wd);
		exit(1);
	}
	if (std) {
		dp = malloc(sizeof(*dp));
		bzero(dp, sizeof(*dp));
		init_dev(dp);
		dp->d_name = strdup(wd);
		dp->d_type = PSEUDO_DEVICE;
		dp->d_count = 1;
		save_dp->d_next = dp;
		goto nextparam;
	}
	for (op = opt; op != NULL; op = op->op_next) {
		if (op->op_value == 0 && opteq(op->op_name, wd)) {
			if (nreqs == 1) {
				free(needs);
				needs = NULL;
			}
			goto nextparam;
		}
	}
	if (nonoptional) {
		printf("%s: the option \"%s\" MUST be specified\n",
			fname, wd);
		exit(1);
	}
invis:
	while ((wd = get_word(fp)) != NULL)
		;
	if (tp == NULL)
		tp = new_fent();
	tp->f_fn = this;
	tp->f_type = INVISIBLE;
	tp->f_needs = needs;
	tp->f_flags = isdup;
	tp->f_special = special;
	tp->f_depends = depends;
	tp->f_clean = clean;
	tp->f_warn = warning;
	goto next;

doneparam:
	if (std == 0 && nreqs == 0) {
		printf("%s: what is %s optional on?\n",
		    fname, this);
		exit(1);
	}

	if (wd != NULL) {
		printf("%s: syntax error describing %s\n",
		    fname, this);
		exit(1);
	}
	if (filetype == PROFILING && profiling == 0)
		goto next;
	if (tp == NULL)
		tp = new_fent();
	tp->f_fn = this;
	tp->f_type = filetype;
	tp->f_flags = 0;
	if (configdep)
		tp->f_flags |= CONFIGDEP;
	if (imp_rule)
		tp->f_flags |= NO_IMPLCT_RULE;
	if (no_obj)
		tp->f_flags |= NO_OBJ;
	if (before_depend)
		tp->f_flags |= BEFORE_DEPEND;
	if (nowerror)
		tp->f_flags |= NOWERROR;
	if (imp_rule)
		tp->f_flags |= NO_IMPLCT_RULE;
	if (no_obj)
		tp->f_flags |= NO_OBJ;
	tp->f_needs = needs;
	tp->f_special = special;
	tp->f_depends = depends;
	tp->f_clean = clean;
	tp->f_warn = warning;
	if (pf && pf->f_type == INVISIBLE)
		pf->f_flags = 1;		/* mark as duplicate */
	goto next;
}

static int
opteq(char *cp, char *dp)
{
	char c, d;

	for (;; cp++, dp++) {
		if (*cp != *dp) {
			c = isupper(*cp) ? tolower(*cp) : *cp;
			d = isupper(*dp) ? tolower(*dp) : *dp;
			if (c != d)
				return(0);
		}
		if (*cp == 0)
			return(1);
	}
}

static void
do_before_depend(FILE *fp)
{
	struct file_list *tp;
	int lpos, len;

	fputs("BEFORE_DEPEND=", fp);
	lpos = 15;
	for (tp = ftab; tp != NULL; tp = tp->f_next)
		if (tp->f_flags & BEFORE_DEPEND) {
			len = strlen(tp->f_fn);
			if ((len = 3 + len) + lpos > 72) {
				lpos = 8;
				fputs("\\\n\t", fp);
			}
			if (tp->f_flags & NO_IMPLCT_RULE)
				fprintf(fp, "%s ", tp->f_fn);
			else
				fprintf(fp, "$S/%s ", tp->f_fn);
			lpos += len + 1;
		}
	if (lpos != 8)
		putc('\n', fp);
}

static void
do_objs(FILE *fp)
{
	struct file_list *tp;
	int lpos, len;
	char *cp, och, *sp;

	fprintf(fp, "OBJS=");
	lpos = 6;
	for (tp = ftab; tp != NULL; tp = tp->f_next) {
		if (tp->f_type == INVISIBLE || tp->f_flags & NO_OBJ)
			continue;
		sp = tail(tp->f_fn);
		cp = sp + (len = strlen(sp)) - 1;
		och = *cp;
		*cp = 'o';
		if (len + lpos > 72) {
			lpos = 8;
			fprintf(fp, "\\\n\t");
		}
		fprintf(fp, "%s ", sp);
		lpos += len + 1;
		*cp = och;
	}
	if (lpos != 8)
		putc('\n', fp);
}

static void
do_cfiles(FILE *fp)
{
	struct file_list *tp;
	int lpos, len;

	fputs("CFILES=", fp);
	lpos = 8;
	for (tp = ftab; tp != NULL; tp = tp->f_next)
		if (tp->f_type != INVISIBLE && tp->f_type != NODEPEND) {
			len = strlen(tp->f_fn);
			if (tp->f_fn[len - 1] != 'c')
				continue;
			if ((len = 3 + len) + lpos > 72) {
				lpos = 8;
				fputs("\\\n\t", fp);
			}
			if (tp->f_type != LOCAL)
				fprintf(fp, "$S/%s ", tp->f_fn);
			else
				fprintf(fp, "%s ", tp->f_fn);

			lpos += len + 1;
		}
	if (lpos != 8)
		putc('\n', fp);
}

static void
do_mfiles(FILE *fp)
{
	struct file_list *tp;
	int lpos, len;

	fputs("MFILES=", fp);
	lpos = 8;
	for (tp = ftab; tp != NULL; tp = tp->f_next)
		if (tp->f_type != INVISIBLE) {
			len = strlen(tp->f_fn);
			if (tp->f_fn[len - 1] != 'm' || tp->f_fn[len - 2] != '.')
				continue;
			if ((len = 3 + len) + lpos > 72) {
				lpos = 8;
				fputs("\\\n\t", fp);
			}
			fprintf(fp, "$S/%s ", tp->f_fn);
			lpos += len + 1;
		}
	if (lpos != 8)
		putc('\n', fp);
}

static void
do_sfiles(FILE *fp)
{
	struct file_list *tp;
	int lpos, len;

	fputs("SFILES=", fp);
	lpos = 8;
	for (tp = ftab; tp != NULL; tp = tp->f_next)
		if (tp->f_type != INVISIBLE) {
			len = strlen(tp->f_fn);
			if (tp->f_fn[len - 1] != 'S' && tp->f_fn[len - 1] != 's')
				continue;
			if ((len = 3 + len) + lpos > 72) {
				lpos = 8;
				fputs("\\\n\t", fp);
			}
			fprintf(fp, "$S/%s ", tp->f_fn);
			lpos += len + 1;
		}
	if (lpos != 8)
		putc('\n', fp);
}


static char *
tail(char *fn)
{
	char *cp;

	cp = strrchr(fn, '/');
	if (cp == NULL)
		return(fn);
	return(cp + 1);
}

/*
 * Create the makerules for each file
 * which is part of the system.
 * Devices are processed with the special c2 option -i
 * which avoids any problem areas with i/o addressing
 * (e.g. for the VAX); assembler files are processed by as.
 */
static void
do_rules(FILE *f)
{
	char *cp, *np, och, *tp;
	struct file_list *ftp;
	char *special;

	for (ftp = ftab; ftp != NULL; ftp = ftp->f_next) {
		if (ftp->f_type == INVISIBLE)
			continue;
		if (ftp->f_warn != NULL)
			printf("WARNING: %s\n", ftp->f_warn);
		cp = (np = ftp->f_fn) + strlen(ftp->f_fn) - 1;
		och = *cp;
		if (ftp->f_flags & NO_IMPLCT_RULE) {
			if (ftp->f_depends)
				fprintf(f, "%s: %s\n", np, ftp->f_depends);
			else
				fprintf(f, "%s: \n", np);
		}
		else {
			*cp = '\0';
			if (och == 'o') {
				fprintf(f, "%so:\n\t-cp $S/%so .\n\n",
					tail(np), np);
				continue;
			}
			if (ftp->f_depends)
				fprintf(f, "%so: $S/%s%c %s\n", tail(np),
					np, och, ftp->f_depends);
			else
				fprintf(f, "%so: $S/%s%c\n", tail(np),
					np, och);
		}
		tp = tail(np);
		special = ftp->f_special;
		if (special == NULL) {
			const char *ftype = NULL;
			static char cmd[128];

			switch (ftp->f_type) {

			case NORMAL:
				ftype = "NORMAL";
				break;

			case PROFILING:
				if (!profiling)
					continue;
				ftype = "PROFILE";
				break;

			default:
				printf("config: don't know rules for %s\n", np);
				break;
			}
			snprintf(cmd, sizeof(cmd), "${%s_%c%s}%s",
			    ftype, toupper(och),
			    ftp->f_flags & CONFIGDEP ? "_C" : "",
			    ftp->f_flags & NOWERROR ? "" : " ${WERROR}");
			special = cmd;
		}
		*cp = och;
		fprintf(f, "\t%s\n\n", special);
	}
}

static void
do_clean(FILE *fp)
{
	struct file_list *tp;
	int lpos, len;

	fputs("CLEAN=", fp);
	lpos = 7;
	for (tp = ftab; tp != NULL; tp = tp->f_next)
		if (tp->f_clean) {
			len = strlen(tp->f_clean);
			if (len + lpos > 72) {
				lpos = 8;
				fputs("\\\n\t", fp);
			}
			fprintf(fp, "%s ", tp->f_clean);
			lpos += len + 1;
		}
	if (lpos != 8)
		putc('\n', fp);
}

char *
raisestr(char *str)
{
	char *cp = str;

	while (*str) {
		if (islower(*str))
			*str = toupper(*str);
		str++;
	}
	return(cp);
}
