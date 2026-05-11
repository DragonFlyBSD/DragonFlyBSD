/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 1997 Doug Rabson
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <sys/param.h>
#include <sys/linker.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
check_path(const char *kldname, const struct kld_file_stat *kldst)
{
    struct stat st1, st2;

    if (strchr(kldname, '/') != NULL || strstr(kldname, ".ko") == NULL)
	return;
    if (stat(kldname, &st1) != 0)
	return;
    if (stat(kldst->pathname, &st2) != 0)
	return;

    if (st1.st_dev != st2.st_dev || st1.st_ino != st2.st_ino)
	warnx("module %s was loaded from %s, not the current directory",
	    kldname, kldst->pathname);
}

static void
usage(void)
{
    fprintf(stderr, "usage: kldload [-nv] file ...\n");
    exit(1);
}

int
main(int argc, char **argv)
{
    int c;
    int errors;
    int fileid;
    int verbose;
    int check_loaded;
    struct kld_file_stat kldst;

    errors = 0;
    verbose = 0;
    check_loaded = 0;
    kldst.version = sizeof(struct kld_file_stat);

    while ((c = getopt(argc, argv, "nv")) != -1)
	switch (c) {
	case 'n':
	    check_loaded = 1;
	    break;
	case 'v':
	    verbose = 1;
	    break;
	default:
	    usage();
	}
    argc -= optind;
    argv += optind;

    if (argc == 0)
	usage();

    while (argc-- != 0) {
	fileid = kldload(argv[0]);
	if (fileid < 0) {
	    if (check_loaded != 0 && errno == EEXIST) {
		if (verbose)
		    printf("%s is already loaded\n", argv[0]);
	    } else {
		switch (errno) {
		case EEXIST:
		    warnx("can't load %s: module already loaded or "
			"in kernel", argv[0]);
		    break;
		case ENOEXEC:
		    warnx("an error occurred while loading module %s. "
			"Please check dmesg(8) for more details.", argv[0]);
		    break;
		default:
		    warn("can't load %s", argv[0]);
		    break;
		}
		errors++;
	    }
	} else {
	    if (kldstat(fileid, &kldst) != 0)
		warn("kldstat(id=%d)", fileid);
	    if (verbose)
		printf("Loaded %s, id=%d, path=%s\n", argv[0], fileid,
		    kldst.pathname);
	    check_path(argv[0], &kldst);
	}
	argv++;
    }

    return errors ? 1 : 0;
}
