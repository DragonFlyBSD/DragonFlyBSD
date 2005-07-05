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
 * $DragonFly: src/sbin/jscan/jscan.c,v 1.3 2005/07/05 00:26:03 dillon Exp $
 */

#include "jscan.h"

enum jmode { JS_NONE, JS_DEBUG, JS_MIRROR };

static void usage(const char *av0);

int debug_opt;

int
main(int ac, char **av)
{
    int ch;
    int i;
    enum jdirection direction = JF_FORWARDS;
    enum jmode jmode = JS_NONE;
    struct jfile *jf;

    while ((ch = getopt(ac, av, "dmr")) != -1) {
	switch(ch) {
	case 'd':
	    debug_opt = 1;
	    if (jmode == JS_NONE)
		jmode = JS_DEBUG;
	    break;
	case 'm':
	    jmode = JS_MIRROR;
	    break;
	case 'r':
	    direction = JF_BACKWARDS;
	    break;
	default:
	    fprintf(stderr, "unknown option: -%c\n", optopt);
	    usage(av[0]);
	}
    }
    if (jmode == JS_NONE)
	usage(av[0]);
    if (jmode == JS_MIRROR && direction == JF_BACKWARDS) {
	fprintf(stderr, "Cannot mirror in reverse scan mode\n");
	usage(av[0]);
    }

    /*
     * Using specified input streams.  If no files are specified, stdin
     * is used.
     */
    if (ac == optind) {
	usage(av[0]);
    } else {
	for (i = optind; i < ac; ++i) {
	    if (strcmp(av[i], "stdin") == 0)
		jf = jopen_fp(stdin, direction);
	    else
		jf = jopen_stream(av[i], direction);
	    if (jf != NULL) {
		switch(jmode) {
		case JS_MIRROR:
		    dump_mirror(jf);
		    break;
		case JS_DEBUG:
		    dump_debug(jf);
		    break;
		case JS_NONE:
		    break;
		}
		jclose_stream(jf);
	    } else {
		fprintf(stderr, "Unable to open %s: %s\n", 
				av[i], strerror(errno));
	    }
	}
    }
    return(0);
}

static void
usage(const char *av0)
{
    fprintf(stderr, "%s [-dm] [journal_file/stdin]*\n", av0);
    exit(1);
}

