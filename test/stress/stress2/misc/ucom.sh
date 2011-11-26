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

# Test disconnecting busy UCOM USB serial dongle

[ `id -u ` -ne 0 ] && echo "Must be root!" && exit 1

. ../default.cfg

tst () {
	for i in `jot 10`; do
		[ -r /dev/cuaU0 ] && break
		sleep 1
	done
	echo $1
	(eval "$@")&
	sleep 10
	ps | grep -qw $! && kill $!
}

if [ $# -eq 0 ]; then
	(
		while true; do
			pstat -t > /dev/null
			sleep 1
		done
	) &
	pid=$!
fi


tst "cat <      /dev/ttyU0"
tst "cat <      /dev/cuaU0"

tst "cu -l      /dev/cuaU0"
tst "cu -l      /dev/ttyU0"

tst "stty -a -f /dev/ttyU0"
tst "stty -a <  /dev/ttyU0"
tst "stty -f    /dev/ttyU0 -a"
tst "tail -F    /dev/ttyU0"
tst "tail       /dev/ttyU0"

[ ! -z "$pid" ] && kill $pid
