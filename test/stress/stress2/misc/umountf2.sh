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

# Test scenario by kris@freebsd.org

# Test problems with "umount -f and fsx. Results in a "KDB: enter: watchdog timeout"

[ `id -u ` -ne 0 ] && echo "Must be root!" && exit 1

fsxc=`find / -name fsx.c | tail -1`
[ -z "fsxc" ] && exit

cc -o /tmp/fsx $fsxc

. ../default.cfg

D=$diskimage
dede $D 1m 1k || exit 1

mount | grep "$mntpoint" | grep md${mdstart}${part} > /dev/null && umount $mntpoint
mdconfig -l | grep md${mdstart} > /dev/null &&  mdconfig -d -u ${mdstart}

mdconfig -a -t vnode -f $D -u ${mdstart}
bsdlabel -w md${mdstart} auto
newfs md${mdstart}${part} > /dev/null 2>&1
mount /dev/md${mdstart}${part} $mntpoint
df -ih $mntpoint
sleep 5
for i in `jot 100`; do
	/tmp/fsx -S $i -q ${mntpoint}/xxx$i &
done
sleep 30
umount -f $mntpoint&
sleep 300
killall fsx
sleep 5
ls -l ${mntpoint}
mdconfig -d -u $mdstart
rm -f $D
ls -l $mntpoint
