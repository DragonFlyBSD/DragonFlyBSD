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

# Quota / snapshot test scenario by Kris@
# Causes spin in ffs_sync or panic in panic: vfs_allocate_syncvnode: insmntque failed

[ `id -u ` -ne 0 ] && echo "Must be root!" && exit 1

D=/var/tmp/diskimage
trap "rm -f $D" 0
dd if=/dev/zero of=$D bs=1m count=1k

mount | grep "/mnt" | grep -q md0 && umount -f /mnt
mdconfig -l | grep -q md0 &&  mdconfig -d -u 0

mdconfig -a -t vnode -f $D -u 0
bsdlabel -w md0 auto
newfs -U  md0c > /dev/null
echo "/dev/md0c /mnt ufs rw,userquota 2 2" >> /etc/fstab
mount /mnt
set `df -ik /mnt | tail -1 | awk '{print $4,$7}'`
export KBLOCKS=$(($1 / 21))
export INODES=$(($2 / 21))
export HOG=1
export INCARNATIONS=40

export QK=$((KBLOCKS / 2))
export QI=$((INODES / 2))
edquota -u -f /mnt -e /mnt:$((QK - 50)):$QK:$((QI - 50 )):$QI pho
quotaon /mnt
sed -i -e '/md0c/d' /etc/fstab
export RUNDIR=/mnt/stressX
mkdir /mnt/stressX
chmod 777 /mnt/stressX
su pho -c "(cd ..;runRUNTIME=1h ./run.sh disk.cfg)"&	# panic: vfs_allocate_syncvnode: insmntque failed
for i in `jot 20`; do
	echo "`date '+%T'` mksnap_ffs /mnt /mnt/.snap/snap$i"
	mksnap_ffs /mnt /mnt/.snap/snap$i
	sleep 1
done
i=$(($(date '+%S') % 20 + 1))
echo "rm -f /mnt/.snap/snap$i"
rm -f /mnt/.snap/snap$i
wait

while mount | grep -q /mnt; do
	umount $([ $((`date '+%s'` % 2)) -eq 0 ] && echo "-f" || echo "") /mnt > /dev/null 2>&1
done
mdconfig -d -u 0
rm -f $D
