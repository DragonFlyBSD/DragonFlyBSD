/*
 * Copyright (c) 2005 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/lib/libsys/genhooks/genhooks.c,v 1.2 2005/12/05 16:48:22 dillon Exp $
 */
/*
 * GENHOOKS [-u] [-l] [-s listfile] [-o outfile] infile(s)
 *
 * Generate assembly hooks for the actual system calls.  This program
 * will either generate hooks for user programs, or it will generate
 * the assembly for the actual syscall layer file.
 */

#include "defs.h"

static void usage(const char *argv0);

int
main(int ac, char **av)
{
    enum { OUTPUT_NONE, OUTPUT_USER, 
	   OUTPUT_LIB, OUTPUT_STANDALONE } output_type = OUTPUT_NONE;
    const char *argv0 = av[0];
    const char *list_prefix = NULL;
    FILE *fo = NULL;
    int i;
    int ch;

    while ((ch = getopt(ac, av, "uls:o:")) != -1) {
	switch(ch) {
	case 'u':
	    output_type = OUTPUT_USER;
	    break;
	case 's':
	    output_type = OUTPUT_STANDALONE;
	    list_prefix = optarg;
	    break;
	case 'l':
	    output_type = OUTPUT_LIB;
	    break;
	case 'o':
	    fo = fopen(optarg, "w");
	    if (fo == NULL) {
		err(1, "Unable to create %s", optarg);
		/* not reached */
	    }
	    break;
	default:
	    usage(argv0);
	    /* not reached */
	}
    }
    ac -= optind;
    av += optind;
    if (output_type == OUTPUT_NONE) {
	errx(1, "No output format specified");
	/* not reached */
    }
    if (ac == 0) {
	errx(1, "No input files specified");
	/* not reached */
    }
    for (i = 0; i < ac; ++i) {
	parse_file(av[i]);
    }
    if (fo == NULL)
	fo = stdout;

    switch(output_type) {
    case OUTPUT_USER:
	output_user(fo);
	break;
    case OUTPUT_LIB:
	output_lib(fo);
	break;
    case OUTPUT_STANDALONE:
	output_standalone(fo, list_prefix);
	break;
    default:
	break;
    }
    if (fo != stdout)
	fclose(fo);
    return(0);
}

static void
usage(const char *argv0)
{
    fprintf(stderr, "%s [-u] [-l] [-s outprefix] [-o outfile] infiles...\n", 
	    argv0);
    exit(1);
}

void *
zalloc(int bytes)
{
    void *ptr;

    ptr = malloc(bytes);
    assert(ptr != NULL);
    bzero(ptr, bytes);
    return(ptr);
}

