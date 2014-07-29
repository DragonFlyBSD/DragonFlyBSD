#!/bin/sh

#
# Copyright (c) 2009 Peter Holm
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

# Test calls with random arguments, in reverse order
# Variation of the syscall test program.

[ `id -u ` -ne 0 ] && echo "Must be root!" && exit 1

. ../default.cfg

odir=`pwd`
cd /tmp
sed '1,/^EOF/d' < $odir/$0 > syscall2.c
cc -o syscall2 -Wall -I $odir/../include -L $odir/../lib syscall2.c -lstress -lutil
rm -f syscall2.c

#kldstat -v | grep -q aio      || kldload aio
#kldstat -v | grep -q mqueuefs || kldload mqueuefs

syscall=`grep SYS_MAXSYSCALL /usr/include/sys/syscall.h | awk '{print $NF}'`

n=$syscall
[ $# -eq 1 ] && n=$1

rm -f /tmp/syscall2.log
while [ $n -gt 0 ]; do
	echo "`date '+%T'` syscall $n" | tee /dev/tty >> /tmp/syscall2.log
	for i in `jot 5`; do
		su ${testuser} -c "sh -c \"/tmp/syscall2 -t 30s -i 100 -h -l 100 -k $n\""
	done
	chflags -R 0 $RUNDIR
	rm -rf $RUNDIR
	n=$((n - 1))
done
rm -f /tmp/syscall2
exit
EOF

/* Call random system calls with random arguments */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <err.h>

#include "stress.h"

static char path[128];
static int num;
static int starting_dir = 0;
uint32_t rb[7][10];

static int ignore[] = {
	SYS_syscall,
	SYS_exit,
	SYS_fork,
	11,			/* 11 is obsolete execv */
	SYS_unmount,
	SYS_reboot,
	SYS_vfork,
	109,			/* 109 is old sigblock */
	111,			/* 111 is old sigsuspend */
	SYS_shutdown,
	SYS___syscall,
	SYS_rfork,
	SYS_sigsuspend,
	SYS_mac_syscall,
	SYS_sigtimedwait,
	SYS_sigwaitinfo,
};

int
setup(int nb)
{
	int i;
	struct rlimit rl;

	umask(0);
	sprintf(path,"%s.%05d", getprogname(), getpid());
	(void)mkdir(path, 0770);
	if (chdir(path) == -1)
		err(1, "chdir(%s), %s:%d", path, __FILE__, __LINE__);
	if ((starting_dir = open(".", 0)) < 0)
		err(1, ".");

	if (op->argc == 1) {
		num = atoi(op->argv[0]);
		for (i = 0; i < sizeof(ignore) / sizeof(ignore[0]); i++)
			if (num == ignore[i]) {
				printf("syscall %d is marked a no test!\n", num);
				exit(1);
			}
	} else {
		num = 0;
		while (num == 0) {
			num = random_int(0, SYS_MAXSYSCALL);
			for (i = 0; i < sizeof(ignore) / sizeof(ignore[0]); i++)
				if (num == ignore[i]) {
					num = 0;
					break;
				}
		}
	}
	if (op->verbose > 1)
		printf("Testing syscall #%d, pid %d\n", num, getpid());

	/* Multiple parallel core dump may panic the kernel with:
	   panic: kmem_malloc(184320): kmem_map too small: 84426752 total allocated
	 */
	rl.rlim_max = rl.rlim_cur = 0;
	if (setrlimit(RLIMIT_CORE, &rl) == -1)
		warn("setrlimit");

	setproctitle("#%d", num);

	return (0);
}

void
cleanup(void)
{
	if (starting_dir != 0) {
		if (fchdir(starting_dir) == -1)
			err(1, "fchdir()");
		(void)system("find . -type d -exec chmod 777 {} \\;");
		(void)system("find . -type f -exec chmod 666 {} \\;");
		(void)system("find . -delete");

		if (chdir("..") == -1)
			err(1, "chdir(..)");
		if (rmdir(path) == -1)
			err(1, "rmdir(%s), %s:%d", path, __FILE__, __LINE__);
	}
	starting_dir = 0;
}

void
rainit(void)
{
	int i, j;

	for (i = 0; i < 7; i++) {
		for (j = 0; j < 10; j++) {
			if (arc4random() % 100 > 20)
				rb[i][j] = arc4random();
			else
				rb[i][j] = (uint32_t) &rb[i][j];
		}
	}
}

uint32_t
ra(int i)
{
	uint32_t r;

	r = arc4random();
	if ((r & 1) == 0)
		r = arc4random();
	else
		r = (uint32_t) &rb[i][0];

	return (r);
}

int
test(void)
{
	int i;
	unsigned int arg1, arg2, arg3, arg4, arg5, arg6, arg7;

	for (i = 0; i < 128; i++) {
		rainit();
		arg1 = ra(0);
		arg2 = ra(1);
		arg3 = ra(2);
		arg4 = ra(3);
		arg5 = ra(4);
		arg6 = ra(5);
		arg7 = ra(6);

		if (op->verbose > 3)
			printf("%2d : syscall(%3d, %x, %x, %x, %x, %x, %x, %x)\n",
				i, num, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
		syscall(num, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
	}

	return (0);
}
