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

[ `id -u ` -ne 0 ] && echo "Must be root!" && exit 1

# Deadlock in umount(1) while out of disk space

D=/usr/tmp/diskimage
#truncate -s 1G $D
truncate -s 250M $D

mount | grep "/mnt" | grep md0c > /dev/null && umount /mnt
mdconfig -l | grep md0 > /dev/null &&  mdconfig -d -u 0

mdconfig -a -t vnode -f $D -u 0
bsdlabel -w md0 auto
newfs -U  md0c > /dev/null
echo "/dev/md0c /mnt ufs rw,userquota 2 2" >> /etc/fstab
mount /mnt
edquota -u -f /mnt -e /mnt:850000:900000:130000:140000 root > /dev/null 2>&1
quotaon /mnt
sed -i -e '/md0c/d' /etc/fstab	# clean up before any panics
export RUNDIR=/mnt/stressX
../testcases/rw/rw -t 10m -i 200 -h -n -v -v&
pid=$!
for i in `jot 5`; do
	echo "`date '+%T'` mksnap_ffs /mnt /mnt/.snap/snap$i"
	mksnap_ffs /mnt /mnt/.snap/snap$i
done
for i in `jot 5`; do
	rm -f /mnt/.snap/snap1
done
kill $pid
false
while mount | grep -q /mnt; do
	umount $([ $((`date '+%s'` % 2)) -eq 0 ] && echo "-f" || echo "") /mnt > /dev/null 2>&1
done
mdconfig -d -u 0
rm -f $D
