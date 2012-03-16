/*
 * Copyright (c) 1995  Peter Wemm
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
 * @(#)mkheaders.c	8.1 (Berkeley) 6/6/93
 * $FreeBSD: src/usr.sbin/config/mkoptions.c,v 1.17.2.3 2001/12/13 19:18:01 dillon Exp $
 */

/*
 * Make all the .h files for the optional entries
 */

#include <ctype.h>
#include <err.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include "config.h"
#include "y.tab.h"

static char *lower(char *);
static void read_options(void);
static void do_option(char *);
static char *tooption(char *);

void
options(void)
{
	char buf[40];
	struct cputype *cp;
	struct opt_list *ol;
	struct opt *op;

	/* Fake the cpu types as options. */
	for (cp = cputype; cp != NULL; cp = cp->cpu_next) {
		op = malloc(sizeof(*op));
		bzero(op, sizeof(*op));
		op->op_name = strdup(cp->cpu_name);
		op->op_next = opt;
		opt = op;
	}	

	if (maxusers == 0) {
		/* printf("maxusers not specified; will auto-size\n"); */
		/* maxusers = 0; */
	} else if (maxusers < 2) {
		puts("minimum of 2 maxusers assumed");
		maxusers = 2;
	} else if (maxusers > 512) {
		printf("warning: maxusers > 512 (%d)\n", maxusers);
	}

	/* Fake MAXUSERS as an option. */
	op = malloc(sizeof(*op));
	bzero(op, sizeof(*op));
	op->op_name = strdup("MAXUSERS");
	snprintf(buf, sizeof(buf), "%d", maxusers);
	op->op_value = strdup(buf);
	op->op_next = opt;
	opt = op;

	read_options();
	for (ol = otab; ol != NULL; ol = ol->o_next)
		do_option(ol->o_name);
	for (op = opt; op != NULL; op = op->op_next) {
		if (!op->op_ownfile) {
			printf("%s:%d: unknown option \"%s\"\n",
			       PREFIX, op->op_line, op->op_name);
			exit(1);
		}
	}
}

/*
 * Generate an <options>.h file
 */

static void
do_option(char *name)
{
	const char *basefile, *file;
	char *inw;
	struct opt_list *ol;
	struct opt *op, *op_head, *topp;
	FILE *inf, *outf;
	char *value;
	char *oldvalue;
	int seen;
	int tidy;

	file = tooption(name);

	/*
	 * Check to see if the option was specified..
	 */
	value = NULL;
	for (op = opt; op != NULL; op = op->op_next) {
		if (strcmp(name, op->op_name) == 0) {
			oldvalue = value;
			value = op->op_value;
			if (value == NULL)
				value = strdup("1");
			if (oldvalue != NULL && strcmp(value, oldvalue) != 0)
				printf(
			    "%s:%d: option \"%s\" redefined from %s to %s\n",
				   PREFIX, op->op_line, op->op_name, oldvalue,
				   value);
			op->op_ownfile++;
		}
	}

	inf = fopen(file, "r");
	if (inf == NULL) {
		outf = fopen(file, "w");
		if (outf == NULL)
			err(1, "%s", file);

		/* was the option in the config file? */
		if (value) {
			fprintf(outf, "#define %s %s\n", name, value);
		} /* else empty file */

		fclose(outf);
		return;
	}
	basefile = "";
	for (ol = otab; ol != NULL; ol = ol->o_next)
		if (strcmp(name, ol->o_name) == 0) {
			basefile = ol->o_file;
			break;
		}
	oldvalue = NULL;
	op_head = NULL;
	seen = 0;
	tidy = 0;
	for (;;) {
		char *cp;
		char *invalue;

		/* get the #define */
		if ((inw = get_word(inf)) == NULL || inw == (char *)EOF)
			break;
		/* get the option name */
		if ((inw = get_word(inf)) == NULL || inw == (char *)EOF)
			break;
		inw = strdup(inw);
		/* get the option value */
		if ((cp = get_word(inf)) == NULL || cp == (char *)EOF)
			break;
		/* option value */
		invalue = strdup(cp); /* malloced */
		if (strcmp(inw, name) == 0) {
			oldvalue = invalue;
			invalue = value;
			seen++;
		}
		for (ol = otab; ol != NULL; ol = ol->o_next)
			if (strcmp(inw, ol->o_name) == 0)
				break;
		if (strcmp(inw, name) != 0 && ol == NULL) {
			printf("WARNING: unknown option `%s' removed from %s\n",
				inw, file);
			tidy++;
		} else if (ol != NULL && strcmp(basefile, ol->o_file) != 0) {
			printf("WARNING: option `%s' moved from %s to %s\n",
				inw, basefile, ol->o_file);
			tidy++;
		} else {
			op = malloc(sizeof(*op));
			bzero(op, sizeof(*op));
			op->op_name = inw;
			op->op_value = invalue;
			op->op_next = op_head;
			op_head = op;
		}

		/* EOL? */
		cp = get_word(inf);
		if (cp == (char *)EOF)
			break;
	}
	fclose(inf);
	if (!tidy && ((value == NULL && oldvalue == NULL) ||
	    (value && oldvalue && strcmp(value, oldvalue) == 0))) {	
		for (op = op_head; op != NULL; op = topp) {
			topp = op->op_next;
			free(op->op_name);
			free(op->op_value);
			free(op);
		}
		return;
	}

	if (value != NULL && !seen) {
		/* New option appears */
		op = malloc(sizeof(*op));
		bzero(op, sizeof(*op));
		op->op_name = strdup(name);
		op->op_value = value != NULL ? strdup(value) : NULL;
		op->op_next = op_head;
		op_head = op;
	}

	outf = fopen(file, "w");
	if (outf == NULL)
		err(1, "%s", file);
	for (op = op_head; op != NULL; op = topp) {
		/* was the option in the config file? */
		if (op->op_value != NULL) {
			fprintf(outf, "#define %s %s\n",
				op->op_name, op->op_value);
		}
		topp = op->op_next;
		free(op->op_name);
		free(op->op_value);
		free(op);
	}
	fclose(outf);
}

/*
 * Find the filename to store the option spec into.
 */
static char *
tooption(char *name)
{
	static char hbuf[MAXPATHLEN];
	char nbuf[MAXPATHLEN];
	struct opt_list *po;

	/* "cannot happen"?  the otab list should be complete.. */
	strlcpy(nbuf, "options.h", sizeof(nbuf));

	for (po = otab ; po != NULL; po = po->o_next) {
		if (strcmp(po->o_name, name) == 0) {
			strlcpy(nbuf, po->o_file, sizeof(nbuf));
			break;
		}
	}

	strlcpy(hbuf, path(nbuf), sizeof(hbuf));
	return(hbuf);
}

/*
 * read the options and options.<machine> files
 */
static void
read_options(void)
{
	FILE *fp;
	char fname[MAXPATHLEN];
	char *wd, *this, *val;
	struct opt_list *po;
	int first = 1;
	char genopt[MAXPATHLEN];

	otab = NULL;
	if (ident == NULL) {
		printf("no ident line specified\n");
		exit(1);
	}
	snprintf(fname, sizeof(fname), "../conf/options");
openit:
	fp = fopen(fname, "r");
	if (fp == NULL) {
		return;
	}
next:
	wd = get_word(fp);
	if (wd == (char *)EOF) {
		fclose(fp);
		if (first == 1) {
			first++;
			snprintf(fname, sizeof(fname),
				 "../platform/%s/conf/options", 
				 platformname);
			fp = fopen(fname, "r");
			if (fp != NULL)
				goto next;
			snprintf(fname, sizeof(fname), "options.%s",
				 platformname);
			goto openit;
		}
		if (first == 2) {
			first++;
			snprintf(fname, sizeof(fname), "options.%s", raisestr(ident));
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
		while (((wd = get_word(fp)) != (char *)EOF) && wd != NULL)
			;
		goto next;
	}
	this = strdup(wd);
	val = get_word(fp);
	if (val == (char *)EOF)
		return;
	if (val == NULL) {
		char *s;
	
		s = strdup(this);
		snprintf(genopt, sizeof(genopt), "opt_%s.h", lower(s));
		val = genopt;
		free(s);
	}
	val = strdup(val);

	for (po = otab; po != NULL; po = po->o_next) {
		if (strcmp(po->o_name, this) == 0) {
			printf("%s: Duplicate option %s.\n",
			       fname, this);
			exit(1);
		}
	}
	
	po = malloc(sizeof(*po));
	bzero(po, sizeof(*po));
	po->o_name = this;
	po->o_file = val;
	po->o_next = otab;
	otab = po;

	goto next;
}

static char *
lower(char *str)
{
	char *cp = str;

	while (*str) {
		if (isupper(*str))
			*str = tolower(*str);
		str++;
	}
	return(cp);
}

