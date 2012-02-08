/*-
 * Copyright (c) 2003 Kip Macy All rights reserved.
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

#include <sys/signal.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/module.h>
#include <sys/checkpoint.h>

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

static
void
usage(const char *prg)
{
    fprintf(stderr, "%s -r file.ckpt\n", prg);
    exit(1);
}

int
main(int ac, char **av)
{
    int fd;
    int error;
    char ch;
    const char *filename = NULL;

    while ((ch = getopt(ac, av, "r:")) != -1) {
	switch(ch) {
	case 'r':
	    filename = optarg;
	    break;
	default:
	    usage(av[0]);
	}
    }
    if (filename == NULL)
	usage(av[0]);

    fd = open(filename, O_RDONLY);
    if (fd < 0) {
	fprintf(stderr, "unable to open %s\n", filename);
	exit(1);
    }
    error = sys_checkpoint(CKPT_THAW, fd, -1, 1);
    if (error)
	fprintf(stderr, "thaw failed error %d %s\n", errno, strerror(errno));
    else
	fprintf(stderr, "Unknown error restoring checkpoint\n");
    return(5);
}
