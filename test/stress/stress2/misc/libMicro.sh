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

# Test using OpenSolaris libmicro-0.4.0.tar.gz benchmark
# Has shown page fault with the cascade_lockf test

. ../default.cfg

odir=`pwd`

cd $RUNDIR
ftp http://www.opensolaris.org/os/project/libmicro/files/libmicro-0.4.0.tar.gz
[ ! -r libmicro-0.4.0.tar.gz ] && exit 1
tar zxfv libmicro-0.4.0.tar.gz
cat > $RUNDIR/libMicro-0.4.0/Makefile.FreeBSD <<2EOF
CC=		gcc

CFLAGS=		-pthread
CPPFLAGS=	-DUSE_SEMOP -D_REENTRANT
MATHLIB=	-lm

ELIDED_BENCHMARKS= \
	cachetocache \
	atomic


include ../Makefile.com
2EOF

cd libMicro-0.4.0
gmake
ed bench <<3EOF
/ARCH
s/arch -k/uname -m/
w
q
3EOF
./bench > output &
for i in `jot $((30 * 60))`; do
	ps | grep -q bench || break
	sleep 1
done
ps | grep bin/connection | grep -v grep | awk '{print $1}' | xargs kill	# hack
cd ..;rm -rf libMicro-0.4.0
