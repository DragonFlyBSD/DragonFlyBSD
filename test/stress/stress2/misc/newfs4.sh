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

[ `id -u ` -ne 0 ] && echo "Must be root!" && exit 1


. ../default.cfg

# Deadlock problems. Test scenario by Lev Serebryakov <lev@freebsd.org>
# newfs -O2 -U -b 65536
# The io programs will get stuck in nbufkv wait state.

odir=`pwd`
cd /tmp
sed '1,/^EOF/d' < $odir/$0 > io.c
cc -o io -Wall io.c
rm -f io.c
cd $odir

mount | grep "$mntpoint" | grep md${mdstart}${part} > /dev/null && umount $mntpoint
mdconfig -l | grep md$mdstart > /dev/null &&  mdconfig -d -u $mdstart

size=9	# Gb
[ `df -k $(dirname $diskimage) | tail -1 | awk '{print $4'}` -lt $((size * 1024 * 1024)) ] && \
		echo "Not enough disk space." && exit 1
truncate -s ${size}G $diskimage

blocksize="-b 65536"
opt="-O2 -U"
mdconfig -a -t vnode -f $diskimage -u $mdstart
bsdlabel -w md$mdstart auto
newfs $blocksize $opt md${mdstart}${part} > /dev/null
mount /dev/md${mdstart}${part} $mntpoint

cd $mntpoint
truncate -s 2g f1
truncate -s 2g f2
truncate -s 2g f3
truncate -s 2g f4
/tmp/io f1 &
/tmp/io f2 &
/tmp/io f3 &
/tmp/io f4 &
wait;wait;wait;wait

while mount | grep "$mntpoint" | grep -q md${mdstart}${part}; do
	umount -f $mntpoint > /dev/null 2>&1
done

mdconfig -d -u $mdstart
rm -f $diskimage
rm -f $RUNDIR/fsx.$$.*
rm -f /tmp/io
exit

EOF
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <err.h>

/* Perform random IO operations on a file */

int
main(int argc, char **argv)
{
	struct stat sb;
	char buf[256];
	off_t bp, maxb;
	int fd;
	long i;

	if (argc != 2) {
		fprintf(stderr, "Usage %s: file\n", argv[0]);
		return (1);
	}
	if ((fd = open(argv[1], O_RDWR)) == -1)
		err(1, "open(%s)", argv[1]);
	if (fstat(fd, &sb) == -1)
		err(1, "fstatf(stdin)");
	maxb = sb.st_size - sizeof(buf);

	for (i = 0; i < 10000; i++) {
		bp = arc4random();
		bp = (bp << 31 | arc4random()) % maxb;
//		printf("%jd\n", bp);

		if (lseek(fd, bp, 0) == -1)
			err(1, "lseek()");
		if (write(fd, buf, sizeof(buf)) != sizeof(buf))
			err(1, "write()");
	}
	close(fd);

	return (0);
}
