/*
 * Copyright (c) 1993,1995 Paul Kranenburg
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Paul Kranenburg.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sbin/ldconfig/ldconfig.c,v 1.31.2.3 2001/07/11 23:59:10 obrien Exp $
 * $DragonFly: src/sbin/ldconfig/ldconfig.c,v 1.6 2004/11/04 13:14:11 joerg Exp $
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <ctype.h>
#include <dirent.h>
#include <elf-hints.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <objformat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ldconfig.h"

#if DEBUG
/* test */
#undef _PATH_ELF_HINTS
#define _PATH_ELF_HINTS		"./ld-elf.so.hints"
#endif

#undef major
#undef minor

static void		usage(void);

int
main(int argc, char **argv)
{
	static const char *hints_file;
	int c, nostd = 0, justread = 0, merge = 0, rescan = 0;

	if (argc > 1 && strcmp(argv[1], "-elf") == 0) {
		/* skip over legacy -elf arg */
		argc--;
		argv++;
	}

	hints_file = _PATH_ELF_HINTS;
	if (argc == 1)
		rescan = 1;
	else while((c = getopt(argc, argv, "Rf:imrsv")) != -1) {
		switch (c) {
		case 'R':
			rescan = 1;
			break;
		case 'f':
			hints_file = optarg;
			break;
		case 'i':
			insecure = 1;
			break;
		case 'm':
			merge = 1;
			break;
		case 'r':
			justread = 1;
			break;
		case 's':
			nostd = 1;
			break;
		default:
			usage();
			break;
		}
	}

	if (justread)
		list_elf_hints(hints_file);
	else
		update_elf_hints(hints_file, argc - optind, argv + optind,
			         merge || rescan);
	return(0);
}

static void
usage(void)
{
	fprintf(stderr,
	"usage: ldconfig [-elf] [-Rimrs] [-f hints_file] [dir | file ...]\n");
	exit(1);
}
