/*
 * Copyright 1997 Sean Eric Fagan
 * Copyright (c) 2004 Liam J. Foy <liamfoy@sepulcrum.org> 
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
 *	This product includes software developed by Sean Eric Fagan
 * 4. Neither the name of the author may be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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
 *
 * $FreeBSD: src/usr.sbin/procctl/procctl.c,v 1.6 2000/02/21 10:22:39 ru Exp $
 * $DragonFly: src/usr.sbin/procctl/procctl.c,v 1.3 2004/12/06 21:13:51 liamfoy Exp $
 */

/*
 * procctl -- clear the event mask, and continue, any specified processes.
 * This is largely an example of how to use the procfs interface; however,
 * for now, it is also sometimes necessary, as a stopped process will not
 * otherwise continue.  (This will be fixed in a later version of the
 * procfs code, almost certainly; however, this program will still be useful
 * for some annoying circumstances.)
 */

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/pioctl.h>

static void	usage(void);

int
main(int argc, char **argv)
{
	int c, vflag, fd;

	vflag = 0;
	while ((c = getopt(argc, argv, "v")) != -1) {
		switch (c) {
		case 'v':
			vflag = 1;
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc == 0)
		usage();

	for (; *argv; ++argv) {
		char buf[MAXPATHLEN];

		snprintf(buf, sizeof(buf), "/proc/%s/mem", *argv);
		fd = open(buf, O_RDWR);
		if (fd == -1) {
			if (!vflag && errno == ENOENT)
				continue;
			warn("cannot open pid %s", *argv);
			continue;
		}

		if (ioctl(fd, PIOCBIC, ~0) == -1)
			warn("cannot clear process %s's event mask", *argv);
		else if (vflag)
			printf("successfully cleared process %s\n", *argv);
			
		if (ioctl(fd, PIOCCONT, 0) == -1 && errno != EINVAL)
			warn("cannot continue process %s", *argv);
		else if (vflag)
			printf("process %s continued\n", *argv);
		close(fd);
	}
	return 0;
}

static void
usage(void)
{

	fprintf(stderr, "usage: procctl [-v] pid...\n");
	exit(EXIT_FAILURE);
}
