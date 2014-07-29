#!/bin/sh

#
# Copyright (c) 2008-2009 Peter Holm
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

[ `id -u ` -ne 0 ] && echo "Must be root!" && exit 1

. ../default.cfg

#kldstat -v | grep -q aio      || kldload aio
#kldstat -v | grep -q mqueuefs || kldload mqueuefs

syscall=`grep SYS_MAXSYSCALL /usr/include/sys/syscall.h | awk '{print $NF}'`

n=$syscall
[ $# -eq 1 ] && n=$1

rm -f /tmp/syscall.log
while [ $n -gt 0 ]; do
	echo "`date '+%T'` syscall $n" | tee /dev/tty >> /tmp/syscall.log
	for i in `jot 5`; do
		su ${testuser} -c "sh -c \"../testcases/syscall/syscall -t 30s -i 100 -h -l 100 -k $n\""
	done
	n=$((n - 1))
	chflags -R 0 $RUNDIR
	rm -rf $RUNDIR
done
