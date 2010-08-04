/*-
 * Copyright (c) 2008 Peter Holm <pho@FreeBSD.org>
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
 *
 */

#include <sys/types.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <err.h>


#include "stress.h"

static unsigned long size;

int
setup(int nb)
{
	int pct;
	unsigned long mem;
	int64_t  swapinfo = 0;
	struct rlimit rlp;

	if (nb == 0) {
		mem = usermem();
		swapinfo = swap();
		if (swapinfo > mem)
			swapinfo = mem;

		if (op->hog == 0)
			pct = random_int(1, 10);

		if (op->hog == 1)
			pct = random_int(10, 20);

		if (op->hog == 2)
			pct = random_int(80, 90);

		if (op->hog >= 3)
			pct = random_int(100, 110);

		if (swapinfo == 0)
			size = mem / 100 * pct;
		else
			size = swapinfo / 100 * pct + mem;

		size = size / op->incarnations;

		if (getrlimit(RLIMIT_DATA, &rlp) < 0)
			err(1,"getrlimit");
		rlp.rlim_cur -= 1024 * 1024;

		if (size > rlp.rlim_cur)
			size = rlp.rlim_cur;
		putval(size);


		if (op->verbose > 1 && nb == 0)
			printf("setup: pid %d, %d%%. Total %luMb\n",
				getpid(), pct, size / 1024 / 1024 * op->incarnations);
	} else
		size = getval();
	return (0);
}

void
cleanup(void)
{
}

int
test(void)
{
	char *c;
	int i, page;
	unsigned long oldsize = size;
	time_t start;

	c = malloc(size);
	while (c == NULL && done_testing == 0) {
		size -=  1024 * 1024;
		c = malloc(size);
	}
	if (op->verbose > 1 && size != oldsize)
		printf("Malloc size changed from %ld Mb to %ld Mb\n",
			oldsize / 1024 / 1024, size / 1024 / 1024);
	page = getpagesize();
	start = time(NULL);	/* Livelock workaround */
	while (done_testing == 0 &&
			(time(NULL) - start) < op->run_time) {
		i = 0;
		while (i < size && done_testing == 0) {
			c[i] = 0;
			i += page;
		}
#if 0
		if (op->hog != 1)
			usleep(1000);
#endif
	}
	free(c);

	return (0);
}
