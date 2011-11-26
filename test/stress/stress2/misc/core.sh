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

# Test multiple (parallel) core dumps

. ../default.cfg

odir=`pwd`

cd /tmp
rm -f core
sed '1,/^EOF/d' < $odir/$0 > core.c
cc -o core -Wall core.c
rm -f core.c
cd $RUNDIR

for i in `jot 2`; do
	for j in `jot 4`; do
		/tmp/core &
	done
	for j in `jot 4`; do
		wait
	done
done
rm -f core
exit
EOF
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <err.h>

#define SIZ 10*1024*1024

int
main(int argc, char **argv)
{
	char *cp = 0;

	if ((cp = malloc(SIZ)) == NULL)
		err(1, "Could not malloc 10Mb!");

	memset(cp, 1, SIZ);

//	printf("sleep %d\n", (120 - (time(NULL) % 120))); fflush(stdout);
	sleep(120 - (time(NULL) % 120));
	raise(SIGSEGV);
//	printf(".\n");

	return (0);
}
