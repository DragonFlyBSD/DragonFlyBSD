/*
 * Copyright (c) 1994 University of Maryland
 * All Rights Reserved.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of U.M. not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  U.M. makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * U.M. DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL U.M.
 * BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author: James da Silva, Systems Design and Analysis Group
 *			   Computer Science Department
 *			   University of Maryland at College Park
 */
/*
 * crunched_main.c - main program for crunched binaries, it branches to a
 * 	particular subprogram based on the value of argv[0].  Also included
 *	is a little program invoked when the crunched binary is called via
 *	its EXECNAME.  This one prints out the list of compiled-in binaries,
 *	or calls one of them based on argv[1].   This allows the testing of
 *	the crunched binary without creating all the links.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>

struct stub {
	const char *name;
	int (*f)(int, char **, char **);
};

static const struct stub entry_points[];

static int crunched_main(int argc, char **, char **);
static int cmpstringp(const void *, const void *);
static void crunched_usage(void) __dead2;


int
main(int argc, char **argv, char **envp)
{
	const char *slash, *basename;
	const struct stub *ep;

	if (argv[0] == NULL || *argv[0] == '\0')
		crunched_usage();

	slash = strrchr(argv[0], '/');
	basename = slash ? slash+1 : argv[0];

	for (ep = entry_points; ep->name != NULL; ep++)
		if (strcmp(basename, ep->name) == 0)
			return ep->f(argc, argv, envp);

	fprintf(stderr, "%s: %s not compiled in\n", EXECNAME, basename);
	crunched_usage();
}


static int
crunched_main(int argc, char **argv, char **envp)
{
	if (argc <= 1)
		crunched_usage();

	return main(--argc, ++argv, envp);
}


static int
cmpstringp(const void *p1, const void *p2)
{
	const char *s1 = *(const char **)p1;
	const char *s2 = *(const char **)p2;
	return strcmp(s1, s2);
}


static void
crunched_usage()
{
	int nprog = 0, columns = 0;
	int i, len;
	const struct stub *ep;
	const char **prognames;

	for (ep = entry_points; ep->name != NULL; ep++)
		nprog++;
	if ((prognames = malloc(nprog * sizeof(char *))) == NULL)
		err(EXIT_FAILURE, "malloc");
	for (i = 0; i < nprog; i++)
		prognames[i] = entry_points[i].name;
	qsort(prognames, nprog, sizeof(char *), cmpstringp);

	fprintf(stderr,
		"usage: %s <prog> <args> ..., where <prog> is one of:\n",
		EXECNAME);
	for (i = 0; i < nprog; i++) {
		if (strcmp(EXECNAME, prognames[i]) == 0)
			continue;
		len = strlen(prognames[i]) + 1;
		if (columns+len < 80)
			columns += len;
		else {
			fprintf(stderr, "\n");
			columns = len;
		}
		fprintf(stderr, " %s", prognames[i]);
	}
	fprintf(stderr, "\n");
	free(prognames);
	exit(1);
}

/* end of crunched_main.c */
