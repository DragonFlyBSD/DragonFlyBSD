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


. ../default.cfg

[ `id -u ` -ne 0 ] && echo "Must be root!" && exit 1

D=$diskimage
dede $D 1m 128 || exit

mount | grep "$mntpoint"    | grep -q /md  && umount -f ${mntpoint}
mdconfig -l | grep -q ${mdstart}  &&  mdconfig -d -u $mdstart

mdconfig -a -t vnode -f $D -u $mdstart

bsdlabel -w md${mdstart} auto
newfs -U md${mdstart}${part} > /dev/null 2>&1
mount /dev/md${mdstart}${part} $mntpoint

mkdir ${mntpoint}/stressX
chmod 777 ${mntpoint}/stressX

export RUNDIR=${mntpoint}/stressX
export runRUNTIME=4m
(cd /home/pho/stress2; ./run.sh disk.cfg) &
sleep 30

for i in `jot 10`; do
	mount $mntpoint -u -o ro > /dev/null 2>&1
	sleep 3
	mount $mntpoint -u -o rw > /dev/null 2>&1
	sleep 3
done
df -i $mntpoint

umount -f $mntpoint    > /dev/null 2>&1
mdconfig -d -u $mdstart
rm -f $D
kill `ps | grep run.sh | grep -v grep | awk '{print $1}'`
