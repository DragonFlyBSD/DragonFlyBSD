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

# Composit test: nullfs2.sh + kinfo.sh

# Kernel page fault with the following non-sleepable locks held from
# nullfs/null_vnops.c:531

[ `id -u ` -ne 0 ] && echo "Must be root!" && exit 1

. ../default.cfg

odir=`pwd`
cd /tmp
sed '1,/^EOF/d;s/60/600/' < $odir/kinfo.sh > kinfo.c
cc -o kinfo -Wall -g kinfo.c -lutil
rm -f kinfo.c
cd $odir

mount | grep -q procfs || mount -t procfs procfs /procfs

for j in `jot 5`; do
	/tmp/kinfo &
done

[ -d mp1 ] || mkdir mp1

mp=`pwd`/mp1
mount | grep -q $mp && umount -f $mp

mount -t nullfs `dirname $RUNDIR` $mp

export RUNDIR=`pwd`/mp1/stressX
export runRUNTIME=10m
(cd ..; ./run.sh marcus.cfg)

umount $mp 2>&1 | grep -v busy

mount | grep -q $mp && umount -f $mp

rm -rf mp1

for j in `jot 5`; do
	wait
done
rm -f /tmp/kinfo
