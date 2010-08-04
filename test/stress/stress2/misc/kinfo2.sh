#!/bin/sh

#
# Copyright (c) 2008 Peter Holm <pho@FreeBSD.org>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$
#

# Test scenario by marcus@freebsd.org

[ `id -u ` -ne 0 ] && echo "Must be root!" && exit 1

. ../default.cfg

odir=`pwd`
cd /tmp
sed '1,/^EOF/d' < $odir/$0 > kinfo.c
cc -o kinfo -Wall kinfo.c -lutil
rm -f kinfo.c

mount | grep -q procfs || mount -t procfs procfs /procfs
for i in `jot 30`; do
	for j in `jot 5`; do
		/tmp/kinfo &
	done

	for j in `jot 5`; do
		wait
	done
done

rm -f /tmp/kinfo
exit
EOF

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/signal.h>
#include <dirent.h>
#include <fcntl.h>
#include <err.h>
#include <strings.h>
#include <sys/wait.h>
#include <libutil.h>

char buf[8096];

void
handler(int i) {
	exit(0);
}

/* Stir /dev/proc */
int
churning(void) {
	pid_t r;
	int fd, status;

	for (;;) {
		r = fork();
		if (r == 0) {
			if ((fd = open("/proc/curproc/mem", O_RDONLY)) == -1)
				err(1, "open(/proc/curproc/mem)");
			bzero(buf, sizeof(buf));
			exit(0);
		}
		if (r < 0) {
			perror("fork");
			exit(2);
		}
		wait(&status);
	}
}

/* Get files for each proc */
void
list(void)
{
	int cnt, fd, n;
	int space = sizeof(buf);
	long base;
	struct dirent *dp;
        struct kinfo_file *freep;
	struct kinfo_vmentry *freep_vm;
	char *bp = buf;
	pid_t pid;
	long l;
	char *dummy;

	if ((fd = open("/proc", O_RDONLY)) == -1)
		err(1, "open(%s)", "/proc");

	do {
		if ((n = getdirentries(fd, bp, space, &base)) == -1)
			err(1, "getdirentries");
		space = space - n;
		bp   = bp + n;
	} while (n != 0);
	close(fd);

	bp = buf;
	dp = (struct dirent *)bp;
	for (;;) {
#if 0
		printf("name: %-10s, inode %7d, type %2d, namelen %d, d_reclen %d\n",
				dp->d_name, dp->d_fileno, dp->d_type, dp->d_namlen,
				dp->d_reclen); fflush(stdout);
#endif

		if (dp->d_type == DT_DIR &&
				(dp->d_name[0] >= '0' && dp->d_name[0] <= '9')) {
			l = strtol(dp->d_name, &dummy, 10);
			pid = l;

			/* The tests start here */
			freep = kinfo_getfile(pid, &cnt);
			free(freep);

			freep_vm = kinfo_getvmmap(pid, &cnt);
			free(freep_vm);
			/* End test */
		}

		bp = bp + dp->d_reclen;
		dp = (struct dirent *)bp;
		if (dp->d_reclen <= 0)
			break;
	}
}

int
main(int argc, char **argv)
{
	pid_t r;
	signal(SIGALRM, handler);
	alarm(60);

	if ((r = fork()) == 0) {
		alarm(60);
		for (;;)
			churning();
	}
	if (r < 0) {
		perror("fork");
		exit(2);
	}

	for (;;)
		list();

	return (0);
}
