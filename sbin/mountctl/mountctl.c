/*
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sbin/mountctl/mountctl.c,v 1.1 2005/01/08 23:22:35 dillon Exp $
 */
/*
 * This utility implements the userland mountctl command which is used to
 * manage high level journaling on mount points.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/mountctl.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

static volatile void usage(void);
static void parse_option_keyword(const char *opt, 
		const char **wopt, const char **xopt);
static int64_t getsize(const char *str);

static void mountctl_scan(void (*func)(const char *, const char *, int),
		const char *keyword, const char *mountpt, int fd);
static void mountctl_list(const char *keyword, const char *mountpt,
		int __unused fd);
static void mountctl_add(const char *keyword, const char *mountpt, int fd);
static void mountctl_delete(const char *keyword, const char *mountpt,
		int __unused fd);
static void mountctl_modify(const char *keyword, const char *mountpt, int fd);

/*
 * For all options 0 means unspecified, -1 means noOPT or nonOPT, and a
 * positive number indicates enabling or execution of the option.
 */
static int freeze_opt;
static int start_opt;
static int close_opt;
static int abort_opt;
static int flush_opt;
static int reversable_opt;
static int twoway_opt;
static int64_t memfifo_opt;
static int64_t swapfifo_opt;

int
main(int ac, char **av)
{
    int fd;
    int ch;
    int aopt = 0;
    int dopt = 0;
    int fopt = 0;
    int lopt = 0;
    int mopt = 0;
    int mimplied = 0;
    const char *wopt = NULL;
    const char *xopt = NULL;
    const char *keyword = NULL;
    const char *mountpt = NULL;

    while ((ch = getopt(ac, av, "adflo:mw:x:ACFSZ")) != -1) {
	switch(ch) {
	case 'a':
	    aopt = 1;
	    if (aopt + dopt + lopt + mopt != 1) {
		fprintf(stderr, "too many action options specified\n");
		usage();
	    }
	    break;
	case 'd':
	    dopt = 1;
	    if (aopt + dopt + lopt + mopt != 1) {
		fprintf(stderr, "too many action options specified\n");
		usage();
	    }
	    break;
	case 'f':
	    fopt = 1;
	    break;
	case 'l':
	    lopt = 1;
	    if (aopt + dopt + lopt + mopt != 1) {
		fprintf(stderr, "too many action options specified\n");
		usage();
	    }
	    break;
	case 'o':
	    parse_option_keyword(optarg, &wopt, &xopt);
	    break;
	case 'm':
	    mopt = 1;
	    if (aopt + dopt + lopt + mopt != 1) {
		fprintf(stderr, "too many action options specified\n");
		usage();
	    }
	    break;
	case 'w':
	    wopt = optarg;
	    mimplied = 1;
	    break;
	case 'x':
	    xopt = optarg;
	    mimplied = 1;
	    break;
	case 'A':
	    mimplied = 1;
	    abort_opt = 1;
	    break;
	case 'C':
	    mimplied = 1;
	    close_opt = 1;
	    break;
	case 'F':
	    mimplied = 1;
	    flush_opt = 1;
	    break;
	case 'S':
	    mimplied = 1;
	    start_opt = 1;
	    break;
	case 'Z':
	    mimplied = 1;
	    freeze_opt = 1;
	    break;
	default:
	    fprintf(stderr, "unknown option: -%c\n", optopt);
	    usage();
	}
    }
    ac -= optind;
    av += optind;

    /*
     * Parse the keyword and/or mount point.
     */
    switch(ac) {
    case 0:
	if (aopt == 0) {
	    fprintf(stderr, "action requires a tag and/or mount "
			    "point to be specified\n");
	    usage();
	}
	break;
    case 1:
	if (av[0][0] == '/')
	    mountpt = av[0];
	else
	    keyword = av[0];
	break;
    case 2:
	keyword = av[0];
	mountpt = av[1];
	if (*mountpt != '/') {
	    fprintf(stderr, "mount points must begin with '/'\n");
	    usage();
	}
	break;
    default:
	fprintf(stderr, "unexpected extra arguments to command\n");
	usage();
    }

    /*
     * Additional sanity checks
     */
    if (aopt + dopt + lopt + mopt + mimplied == 0) {
	fprintf(stderr, "no action or implied action options were specified\n");
	usage();
    }
    if (mimplied && aopt + dopt + lopt == 0)
	mopt = 1;
    if ((wopt || xopt) && !(aopt || mopt)) {
	fprintf(stderr, "-w/-x/path/fd options may only be used with -m/-a\n");
	usage();
    }
    if (aopt && (keyword == NULL || mountpt == NULL)) {
	fprintf(stderr, "a keyword AND a mountpt must be specified "
			"when adding a journal\n");
	usage();
    }
    if (fopt == 0 && mopt + dopt && keyword == NULL && mountpt == NULL) {
	fprintf(stderr, "a keyword, a mountpt, or both must be specified "
			"when modifying or deleting a journal, unless "
			"-f is also specified for safety\n");
	usage();
    }

    /*
     * Open the journaling file descriptor if required.
     */
    if (wopt && xopt) {
	fprintf(stderr, "you must specify only one of -w/-x/path/fd\n");
	exit(1);
    } else if (wopt) {
	if ((fd = open(wopt, O_RDWR|O_CREAT|O_APPEND, 0666)) < 0) {
	    fprintf(stderr, "unable to create %s: %s\n", wopt, strerror(errno));
	    exit(1);
	}
    } else if (xopt) {
	fd = strtol(xopt, NULL, 0);
    } else if (aopt) {
	fd = 1;		/* stdout default for -a */
    } else {
	fd = -1;
    }

    /*
     * And finally execute the core command.
     */
    if (lopt)
	mountctl_scan(mountctl_list, keyword, mountpt, fd);
    if (aopt)
	mountctl_add(keyword, mountpt, fd);
    if (dopt)
	mountctl_scan(mountctl_delete, keyword, mountpt, -1);
    if (mopt)
	mountctl_scan(mountctl_modify, keyword, mountpt, fd);

    return(0);
}

static void
parse_option_keyword(const char *opt, const char **wopt, const char **xopt)
{
    char *str = strdup(opt);
    char *name;
    char *val;
    int negate;
    int hasval;
    int cannotnegate;

    /*
     * multiple comma delimited options may be specified.
     */
    while ((name = strsep(&str, ",")) != NULL) {
	/*
	 * some options have associated data.
	 */
	if ((val = strchr(name, '=')) != NULL)
	    *val++ = 0;

	/*
	 * options beginning with 'no' or 'non' are negated.  A positive
	 * number means not negated, a negative number means negated.
	 */
	negate = 1;
	cannotnegate = 0;
	hasval = 0;
	if (strncmp(name, "non", 3) == 0) {
	    name += 3;
	    negate = -1;
	} else if (strncmp(name, "no", 2) == 0) {
	    name += 2;
	    negate = -1;
	}

	/*
	 * Parse supported options
	 */
	if (strcmp(name, "reversable") == 0) {
	    reversable_opt = negate;
	} else if (strcmp(name, "twoway") == 0) {
	    twoway_opt = negate;
	} else if (strcmp(name, "memfifo") == 0) {
	    cannotnegate = 1;
	    hasval = 1;
	    if (val) {
		if ((memfifo_opt = getsize(val)) == 0)
		    memfifo_opt = -1;
	    }
	} else if (strcmp(name, "swapfifo") == 0) {
	    if (val) {
		hasval = 1;
		if ((swapfifo_opt = getsize(val)) == 0)
		    swapfifo_opt = -1;
	    } else if (negate < 0) {
		swapfifo_opt = -1;
	    } else {
		hasval = 1;	/* force error */
	    }
	} else if (strcmp(name, "fd") == 0) {
	    cannotnegate = 1;
	    hasval = 1;
	    if (val)
		*xopt = val;
	} else if (strcmp(name, "path") == 0) {
	    cannotnegate = 1;
	    hasval = 1;
	    if (val)
		*wopt = val;
	} else if (strcmp(name, "freeze") == 0 || strcmp(name, "stop") == 0) {
	    if (negate < 0)
		start_opt = -negate;
	    else
		freeze_opt = negate;
	} else if (strcmp(name, "start") == 0) {
	    if (negate < 0)
		freeze_opt = -negate;
	    else
		start_opt = negate;
	} else if (strcmp(name, "close") == 0) {
	    close_opt = negate;
	} else if (strcmp(name, "abort") == 0) {
	    abort_opt = negate;
	} else if (strcmp(name, "flush") == 0) {
	    flush_opt = negate;
	} else {
	    fprintf(stderr, "unknown option keyword: %s\n", name);
	    exit(1);
	}

	/*
	 * Sanity checks
	 */
	if (cannotnegate && negate < 0) {
	    fprintf(stderr, "option %s may not be negated\n", name);
	    exit(1);
	}
	if (hasval && val == NULL) {
	    fprintf(stderr, "option %s requires assigned data\n", name);
	    exit(1);
	}
	if (hasval == 0 && val) {
	    fprintf(stderr, "option %s does not take an assignment\n", name);
	    exit(1);
	}

    }
}

static void
mountctl_scan(void (*func)(const char *, const char *, int),
	    const char *keyword, const char *mountpt, int fd)
{
    fprintf(stderr, "scan not yet implemented\n");
}

static void
mountctl_list(const char *keyword, const char *mountpt, int __unused fd)
{
    fprintf(stderr, "list not yet implemented\n");
}

static void
mountctl_add(const char *keyword, const char *mountpt, int fd)
{
    fprintf(stderr, "add not yet implemented\n");
}

static void
mountctl_delete(const char *keyword, const char *mountpt, int __unused fd)
{
    fprintf(stderr, "delete not yet implemented\n");
}

static void
mountctl_modify(const char *keyword, const char *mountpt, int fd)
{
    fprintf(stderr, "modify not yet implemented\n");
}


static volatile
void
usage(void)
{
    printf(
	" mountctl -l [tag/mountpt | tag mountpt]\n"
	" mountctl -a [-w output_path] [-x filedesc]\n"
	"             [-o option] [-o option ...] tag mountpt\n"
	" mountctl -d [tag/mountpt | tag mountpt]\n"
	" mountctl -m [-o option] [-o option ...] [tag/mountpt | tag mountpt]\n"
	" mountctl -FZSCA [tag/mountpt | tag mountpt]\n"
    );
    exit(1);
}

static
int64_t
getsize(const char *str)
{
    const char *suffix;
    int64_t val;

    val = strtoll(str, &suffix, 0);
    if (suffix) {
	switch(*suffix) {
	case 'b':
	    break;
	case 't':
	    val *= 1024;
	    /* fall through */
	case 'g':
	    val *= 1024;
	    /* fall through */
	case 'm':
	    val *= 1024;
	    /* fall through */
	case 'k':
	    val *= 1024;
	    /* fall through */
	    break;
	default:
	    fprintf(stderr, "data value '%s' has unknown suffix\n", str);
	    exit(1);
	}
    }
    return(val);
}

