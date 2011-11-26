/*
 *             COPYRIGHT (c) 1990 BY             *
 *  GEORGE J. CARRETTE, CONCORD, MASSACHUSETTS.  *
 *             ALL RIGHTS RESERVED               *

Permission to use, copy, modify, distribute and sell this software
and its documentation for any purpose and without fee is hereby
granted, provided that the above copyright notice appear in all copies
and that both that copyright notice and this permission notice appear
in supporting documentation, and that the name of the author
not be used in advertising or publicity pertaining to distribution
of the software without specific, written prior permission.

THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
HE BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

This code is based on crashme.c

*/

#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>
#include <err.h>

#include "stress.h"

#define BUFS 64

char buf [BUFS];
pid_t pid;

void (*sub)();

void
proto(void) {
	int i = 2;
	printf("Hello, world (%d)\n", i);
	return;
}

void
alter(void) {	/* Change one byte in the code */
	int i;
	i = random() % BUFS;
	buf[i] = random() & 0xff;
}

void
hand(int i) {	/* alarm handler */
	if (pid != 0) {
		kill(pid, SIGHUP);
		kill(pid, SIGKILL);
	}
	exit(1);
}

int
setup(int nb)
{
	return (0);
}

void
cleanup(void)
{
}

int
test(void)
{
	pid_t pid;
	int i, status;

	for (i = 0; i < 512; i++) {
		if (i % 10 == 0)
			bcopy(proto, buf, BUFS);
		alter();

		if ((pid = fork()) == 0) {
			signal(SIGALRM, hand);
#if 0
			signal(SIGILL,  hand);
			signal(SIGFPE,  hand);
			signal(SIGSEGV, hand);
			signal(SIGBUS,  hand);
			signal(SIGURG,  hand);
			signal(SIGSYS,  hand);
			signal(SIGTRAP, hand);
#endif
			alarm(2);

			(*sub)();

			exit(EXIT_SUCCESS);

		} else if (pid > 0) {
			signal(SIGALRM, hand);
			alarm(3);
			if (waitpid(pid, &status, 0) == -1)
				warn("waitpid(%d)", pid);
			alarm(0);
			kill(pid, SIGINT);
		} else
			err(1, "fork(), %s:%d",  __FILE__, __LINE__);
	}

	return (0);
}
