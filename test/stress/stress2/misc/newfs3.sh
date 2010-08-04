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

# phk has seen freezes with this newfs option: "-b 32768 -f  4096 -O2"
#
# Deadlocks seen with this test and:
#          newfs -b 4096 -f 4096 -O2 md0c on a 128 Mb FS
#          newfs -b 4096 -f 1024 -O2 md0c on a 64 Mb FS
# 20070505 newfs -b 4096 -f 4096 -O2 md0c on a 32 Mb FS: panic: lockmgr: locking against myself


[ `id -u ` -ne 0 ] && echo "Must be root!" && exit 1

. ../default.cfg

size=$((32 * 1024 * 1024))
opt="-O2"	# newfs option. Eg. -U

mount | grep "$mntpoint" | grep md${mdstart}${part} > /dev/null && umount $mntpoint
mdconfig -l | grep md${mdstart} > /dev/null &&  mdconfig -d -u ${mdstart}

while [ $size -le $((128 * 1024 * 1024)) ]; do
	truncate -s $size $diskimage
	mdconfig -a -t vnode -f $diskimage -u ${mdstart}
	disklabel -r -w md${mdstart} auto
	blocksize=4096
	while [ $blocksize -le 65536 ]; do
		for i in 1 2 4 8; do
			fragsize=$((blocksize / i))
			echo "newfs -b $blocksize -f $fragsize $opt md${mdstart}${part} on a $((size / 1024 / 1024)) Mb FS"
			newfs -b $blocksize -f $fragsize $opt md${mdstart}${part} > /dev/null 2>&1
			mount /dev/md${mdstart}${part} $mntpoint
			export RUNDIR=$mntpoint/stressX
			export runRUNTIME=5m
			(cd /home/pho/stress2; ./run.sh disk.cfg)
			while mount | grep "$mntpoint" | grep -q md${mdstart}${part}; do
				umount $mntpoint > /dev/null 2>&1
			done
		done
		blocksize=$((blocksize * 2))
	done
	mdconfig -d -u ${mdstart}
	size=$((size + 32 * 1024 * 1024))
done
rm -f $diskimage
