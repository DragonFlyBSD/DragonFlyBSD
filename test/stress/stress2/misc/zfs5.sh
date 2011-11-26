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

# Simple zfs test of vdev as a file and snapshot clones

[ `id -u ` -ne 0 ] && echo "Must be root!" && exit 1

. ../default.cfg

kldstat -v | grep -q zfs.ko  || kldload zfs.ko

d1=${diskimage}.1
d2=${diskimage}.2

dd if=/dev/zero of=$d1 bs=1m count=1k 2>&1 | egrep -v "records|transferred"
dd if=/dev/zero of=$d2 bs=1m count=1k 2>&1 | egrep -v "records|transferred"

zpool create tank $d1 $d2
zfs create tank/test
zfs set quota=100m tank/test

export RUNDIR=/tank/test/stressX
export runRUNTIME=10m
(cd ..; ./run.sh vfs.cfg) &

for i in `jot 20`; do
	zfs snapshot tank/test@snap$i
	zfs clone    tank/test@snap$i tank/snap$i
done
for i in `jot 20`; do
	zfs destroy tank/snap$i
	zfs destroy tank/test@snap$i
done
wait

zfs destroy -r tank
zpool destroy tank

rm -rf $d1 $d2
