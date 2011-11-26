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

# Testing problem with premature disk full problem with symlinks

[ `id -u ` -ne 0 ] && echo "Must be root!" && exit 1

. ../default.cfg

D=$diskimage
dede $D 1m 1024 || exit 1

odir=`pwd`
dir=$mntpoint

cd /tmp
sed '1,/^EOF/d' < $odir/$0 > symlink.c
cc -o symlink -Wall symlink.c
rm -f symlink.c
cd $odir

mount | grep "$mntpoint" | grep md${mdstart} > /dev/null && umount $mntpoint
mdconfig -l | grep md${mdstart} > /dev/null &&  mdconfig -d -u ${mdstart}

mdconfig -a -t vnode -f $D -u ${mdstart}


tst() {
	cd $dir
	df -ik $mntpoint
	i=`df -ik $mntpoint | tail -1 | awk '{printf "%d\n", ($7 - 500)/2}'`
	[ $i -gt 20000 ] && i=20000

	for k in `jot 5`; do
		for j in `jot 2`; do
			/tmp/symlink $i &
		done
		for j in `jot 2`; do
			wait
		done
		df -ik $mntpoint | tail -1
#		sleep 30	# With this enabled, soft update also works
	done
	cd $odir
}

for i in "" "-U"; do
	echo "newfs $i /dev/md${mdstart}"
	newfs $i /dev/md${mdstart} > /dev/null 2>&1
	mount /dev/md${mdstart} ${mntpoint}

	tst

	umount -f ${mntpoint}
done
mdconfig -d -u $mdstart
exit
EOF
#include <sys/types.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <errno.h>
#include <err.h>

int
main(int argc, char **argv)
{
	int i, j;
	int64_t size;
	pid_t pid;
	char file[128];

	size = atol(argv[1]);

//	printf("Creating %jd symlinks...\n", size); fflush(stdout);
	pid = getpid();
	for (j = 0; j < size; j++) {
		sprintf(file,"p%05d.%05d", pid, j);
		if (symlink("/mnt/not/there", file) == -1) {
			if (errno != EINTR) {
				warn("symlink(%s)", file);
				printf("break out at %d, errno %d\n", j, errno);
				break;
			}
		}
	}

//	printf("Deleting %jd files...\n", size); fflush(stdout);
	for (i = --j; i >= 0; i--) {
		sprintf(file,"p%05d.%05d", pid, i);
		if (unlink(file) == -1)
			err(3, "unlink(%s)", file);

	}
	return (0);
}
