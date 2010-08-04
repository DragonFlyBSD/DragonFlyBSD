#!/bin/sh

#
# Copyright (c) 2009 Peter Holm <pho@FreeBSD.org>
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

# With lookup_shared=1 rename() will fail from time to time with ENOENT and
# the following stat() will succeed. (Variation of rename.sh)

here=`pwd`
cd /tmp
sed '1,/^EOF/d' < $here/$0 > rename.c
cc -o rename -Wall rename.c
rm -f rename.c
cd $here

rm -rf /tmp/rename.dir.*
for i in `jot 10`; do
	for j in `jot 10`; do
		/tmp/rename &
	done
	for j in `jot 10`; do
		wait
	done
done
rm -rf /tmp/rename.dir.*
exit 0
EOF
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static char dir1[128];
static char dir2[128];

int
main(int argc, char **argv)
{
	int i;
	struct stat sb;

	sprintf(dir1, "/tmp/rename.dir.%d", getpid());
	sprintf(dir2, "/tmp/rename.dir.2.%d", getpid());
	if (mkdir(dir1, 0700) == -1)
		err(1, "mkdir(%s)", dir1);

	if (chdir(dir1) == -1)
		err(1, "chdir(%s)", dir1);
	if (chdir("..") == -1)
		err(1, "chdir(%s)", "..");

	for (i = 0; i < 100000; i++) {
		if (rename(dir1, dir2) == -1) {
			warn("rename(%s, %s)", dir1, dir2);
			if (stat(dir1, &sb) == -1)
				err(1, "stat(%s)", dir1);
			else
				errx(1, "stat(%s) succeeded!", dir1);
		}
		if (rename(dir2, dir1) == -1) {
			warn("rename(%s, %s)", dir2, dir1);
			if (stat(dir2, &sb) == -1)
				err(1, "stat(%s)", dir2);
			else
				errx(1, "stat(%s) succeeded!", dir2);
		}
	}

	if (rmdir(dir1) == -1)
		err(1, "rmdir(%s)", dir1);

	return (0);
}
