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

. ../default.cfg

D=$diskimage
trap "rm -f $D" 0
dede $D 1m 1k || exit 1

mount | grep "${mntpoint}" | grep -q md${mdstart} && umount -f ${mntpoint}
mdconfig -l | grep -q md${mdstart} &&  mdconfig -d -u ${mdstart}

mdconfig -a -t vnode -f $D -u ${mdstart}
bsdlabel -w md${mdstart} auto
newfs -U  md${mdstart}${part} > /dev/null
echo "/dev/md${mdstart}${part} ${mntpoint} ufs rw,userquota 2 2" >> /etc/fstab
mount ${mntpoint}
set `df -ik ${mntpoint} | tail -1 | awk '{print $4,$7}'`
export KBLOCKS=$(($1 / 21))
export INODES=$(($2 / 21))
export HOG=1
export INCARNATIONS=40

export QK=$((KBLOCKS / 2))
export QI=$((INODES / 2))
edquota -u -f ${mntpoint} -e ${mntpoint}:$((QK - 50)):$QK:$((QI - 50 )):$QI ${testuser}
quotaon ${mntpoint}
sed -i -e "/md${mdstart}${part}/d" /etc/fstab
export RUNDIR=${mntpoint}/stressX
mkdir ${mntpoint}/stressX
chmod 777 ${mntpoint}/stressX
su ${testuser} -c 'sh -c "(cd ..;runRUNTIME=20m ./run.sh disk.cfg)"&'   # Deadlock
for i in `jot 20`; do
	echo "`date '+%T'` mksnap_ffs ${mntpoint} ${mntpoint}/.snap/snap$i"
	mksnap_ffs ${mntpoint} ${mntpoint}/.snap/snap$i
	sleep 1
done
i=$(($(date '+%S') % 20 + 1))
echo "rm -f ${mntpoint}/.snap/snap$i"
rm -f ${mntpoint}/.snap/snap$i
wait

while mount | grep -q ${mntpoint}; do
	umount $([ $((`date '+%s'` % 2)) -eq 0 ] && echo "-f" || echo "") ${mntpoint} > /dev/null 2>&1
done
mdconfig -d -u ${mdstart}
rm -f $D
