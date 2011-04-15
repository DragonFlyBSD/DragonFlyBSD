/*
 * Copyright (c) 2002, 2003 Greg Lehey
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * This software is provided by the author ``as is'' and any express
 * or implied warranties, including, but not limited to, the implied
 * warranties of merchantability and fitness for a particular purpose
 * are disclaimed.  In no event shall the author be liable for any
 * direct, indirect, incidental, special, exemplary, or consequential
 * damages (including, but not limited to, procurement of substitute
 * goods or services; loss of use, data, or profits; or business
 * interruption) however caused and on any theory of liability,
 * whether in contract, strict liability, or tort (including
 * negligence or otherwise) arising in any way out of the use of this
 * software, even if advised of the possibility of such damage.
 */
/* $Id: asf.c,v 1.6 2003/11/04 06:38:37 green Exp $ */
/* $FreeBSD: src/usr.sbin/asf/asf.c,v 1.6 2003/11/04 06:38:37 green Exp $ */

#define MAXLINE 1024
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fts.h>
#include <unistd.h>

#define MAXTOKEN 10
const char *modules_path;		/* path relative to kernel
					 * build directory */
const char *outfile;			/* and where to write the output */

/*
 * Take a blank separated list of tokens and turn it into a list of
 * individual nul-delimited strings.  Build a list of pointers at
 * token, which must have enough space for the tokens.  Return the
 * number of tokens, or -1 on error (typically a missing string
 * delimiter).
 */
static int
tokenize(char *cptr, char *token[], int maxtoken)
{
    char delim;				/* delimiter to search for */
    int tokennr;			/* index of this token */

    for (tokennr = 0; tokennr < maxtoken;) {
	while (isspace(*cptr))
	    cptr++;			/* skip initial white space */
	if ((*cptr == '\0') || (*cptr == '\n')
	    || (*cptr == '#'))		/* end of line */
	    return tokennr;		/* return number of tokens found */
	delim = *cptr;
	token[tokennr] = cptr;		/* point to it */
	tokennr++;			/* one more */
	if (tokennr == maxtoken)	/* run off the end? */
	    return tokennr;
	if ((delim == '\'') || (delim == '"')) { /* delimitered */
	    for (;;) {
		cptr++;
		if ((*cptr == delim)
		    && (cptr[-1] != '\\')) { /* found the partner */
		    cptr++;		/* move on past */
		    if (!isspace(*cptr)) /* no space after closing quote */
			return -1;
		    *cptr++ = '\0';	/* delimit */
		} else if ((*cptr == '\0')
		    || (*cptr == '\n'))	/* end of line */
		    return -1;
	    }
	} else {			/* not quoted */
	    while ((*cptr != '\0') && (!isspace(*cptr)) && (*cptr != '\n'))
		cptr++;
	    if (*cptr != '\0')		/* not end of the line, */
		*cptr++ = '\0';		/* delimit and move to the next */
	}
    }
    return maxtoken;			/* can't get here */
}

static char *
findmodule(char *mod_path, const char *module_name)
{
    char *const path_argv[2] = { mod_path, NULL };
    char *module_path = NULL;
    size_t module_name_len = strlen(module_name);
    FTS *fts;
    FTSENT *ftsent;

    if (mod_path == NULL) {
	fprintf(stderr,
	    "Can't allocate memory to traverse a path: %s (%d)\n",
	    strerror(errno),
	    errno);
	exit(1);
    }
    fts = fts_open(path_argv, FTS_PHYSICAL | FTS_NOCHDIR, NULL);
    if (fts == NULL) {
	fprintf(stderr,
	    "Can't begin traversing path %s: %s (%d)\n",
	    mod_path,
	    strerror(errno),
	    errno);
	exit(1);
    }
    while ((ftsent = fts_read(fts)) != NULL) {
	if (ftsent->fts_info == FTS_DNR ||
	    ftsent->fts_info == FTS_ERR ||
	    ftsent->fts_info == FTS_NS) {
	    fprintf(stderr,
		"Error while traversing path %s: %s (%d)\n",
		mod_path,
		strerror(errno),
		errno);
	    exit(1);
	}
	if (ftsent->fts_info != FTS_F ||
	    ftsent->fts_namelen != module_name_len ||
	    memcmp(module_name, ftsent->fts_name, module_name_len) != 0)
		continue;
	if (asprintf(&module_path,
	    "%.*s",
	    (int)ftsent->fts_pathlen,
	    ftsent->fts_path) == -1) {
	    fprintf(stderr,
		"Can't allocate memory traversing path %s: %s (%d)\n",
		mod_path,
		strerror(errno),
		errno);
	    exit(1);
	}
	break;
    }
    if (ftsent == NULL && errno != 0) {
	fprintf(stderr,
	    "Couldn't complete traversing path %s: %s (%d)\n",
	    mod_path,
	    strerror(errno),
	    errno);
	exit(1);
    }
    fts_close(fts);
    free(mod_path);
    return (module_path);
}

static void
usage(const char *myname)
{
    fprintf(stderr,
	"Usage:\n"
	"%s [-a] [-f] [-k] [modules-path [outfile]]\n\n"
	"\t-a\tappend to outfile)\n"
	"\t-f\tfind the module in any subdirectory of module-path\n"
	"\t-k\ttake input from kldstat(8)\n",
	myname);
}

int
main(int argc, char *argv[])
{
    char buf[MAXLINE];
    FILE *kldstat;
    FILE *objcopy;
    FILE *out;				/* output file */
    char ocbuf[MAXLINE];
    int tokens;				/* number of tokens on line */
    int ch;
    const char *filemode = "w";		/* mode for outfile */
    char cwd[MAXPATHLEN];		/* current directory */
    char *token[MAXTOKEN];
    int dofind = 0;

    getcwd(cwd, MAXPATHLEN);		/* find where we are */
    kldstat = stdin;
    while ((ch = getopt(argc, argv, "afk")) != -1) {
	switch (ch) {
	case 'k': /* get input from kldstat(8) */
	    if (!(kldstat = popen("kldstat", "r"))) {
		perror("Can't start kldstat");
		return 1;
	    }
	    break;
	case 'a': /* append to outfile */
	    filemode = "a";
	    break;
	case 'f': /* find .ko (recursively) */
	    dofind = 1;
	    break;
	default:
	    usage(argv[0]);
	    return 1;
	}
    }

    argv += optind;
    argc -= optind;

    if (argc >= 1) {
	modules_path = argv[0];
	argc--;
	argv++;
    }

    if (argc >= 1) {
	outfile = argv[0];
	argc--;
	argv++;
    }

    if (argc > 0) {
	fprintf(stderr,
	    "Extraneous startup information: \"%s\", aborting\n",
	    argv[0]);
	usage(getprogname());
	return 1;
    }
    if (modules_path == NULL)
	modules_path = "/boot/kernel";
    if (outfile == NULL)
	outfile = ".asf";
    if ((out = fopen(outfile, filemode)) == NULL) {
	fprintf(stderr,
	    "Can't open output file %s: %s (%d)\n",
	    outfile,
	    strerror(errno),
	    errno);
	return 1;
    }
    while (fgets(buf, MAXLINE, kldstat)) {
	if ((!(strstr(buf, "kernel")))
	    && buf[0] != 'I') {
	    long long base;
	    long long textaddr = 0;
	    long long dataaddr = 0;
	    long long bssaddr = 0;

	    tokens = tokenize(buf, token, MAXTOKEN);
	    if (tokens <= 1)
		continue;
	    base = strtoll(token[2], NULL, 16);
	    if (!dofind) {
		snprintf(ocbuf,
		    MAXLINE,
		    "/usr/bin/objdump --section-headers %s/%s",
		    modules_path,
		    token[4]);
	    } else {
		char *modpath;
		
		modpath = findmodule(strdup(modules_path), token[4]);
		if (modpath == NULL)
		    continue;
		snprintf(ocbuf,
		    MAXLINE,
		    "/usr/bin/objdump --section-headers %s",
		    modpath);
		free(modpath);
	    }
	    if (!(objcopy = popen(ocbuf, "r"))) {
		fprintf(stderr,
		    "Can't start %s: %s (%d)\n",
		    ocbuf,
		    strerror(errno),
		    errno);
		return 1;
	    }
	    while (fgets(ocbuf, MAXLINE, objcopy)) {
		int octokens;
		char *octoken[MAXTOKEN];

		octokens = tokenize(ocbuf, octoken, MAXTOKEN);
		if (octokens > 1) {
		    if (!strcmp(octoken[1], ".text"))
			textaddr = strtoll(octoken[3], NULL, 16) + base;
		    else if (!strcmp(octoken[1], ".data"))
			dataaddr = strtoll(octoken[3], NULL, 16) + base;
		    else if (!strcmp(octoken[1], ".bss"))
			bssaddr = strtoll(octoken[3], NULL, 16) + base;
		}
	    }
	    if (textaddr) {		/* we must have a text address */
		if (!dofind) {
		    fprintf(out,
			"add-symbol-file %s%s%s/%s 0x%llx",
			modules_path[0] != '/' ? cwd : "",
			modules_path[0] != '/' ? "/" : "",
			modules_path,
			token[4],
			textaddr);
		} else {
		    char *modpath;

		    modpath = findmodule(strdup(modules_path), token[4]);
		    if (modpath == NULL)
			continue;
		    fprintf(out,
			"add-symbol-file %s 0x%llx",
			modpath,
			textaddr);
		    free(modpath);
		}
		if (dataaddr)
		    fprintf(out, " -s .data 0x%llx", dataaddr);
		if (bssaddr)
		    fprintf(out, " -s .bss 0x%llx", bssaddr);
		fprintf(out, "\n");
	    }
	}
    }
    return 0;
}
