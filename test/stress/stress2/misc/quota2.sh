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

. ../default.cfg

D=$diskimage
trap "rm -f $D" 0
dede $D 1m 128 || exit 1

mount | grep "${mntpoint}" | grep md${mdstart}${part} > /dev/null && umount ${mntpoint}
mdconfig -l | grep md${mdstart} > /dev/null &&  mdconfig -d -u ${mdstart}

mdconfig -a -t vnode -f $D -u ${mdstart}
bsdlabel -w md${mdstart} auto
newfs -U  md${mdstart}${part} > /dev/null
echo "/dev/md${mdstart}${part} ${mntpoint} ufs rw,userquota 2 2" >> /etc/fstab
mount ${mntpoint}
edquota -u -f ${mntpoint} -e ${mntpoint}:100000:110000:15000:16000 root
quotacheck ${mntpoint}
quotaon ${mntpoint}
quota root
df -i ${mntpoint}
sed -i -e "/md${mdstart}${part}/d" /etc/fstab	# clean up before any panics
export RUNDIR=${mntpoint}/stressX
export runRUNTIME=10m            # Run tests for 10 minutes
(cd /home/pho/stress2; ./run.sh disk.cfg)
false
while [ $? -ne 0 ]; do
	umount ${mntpoint} > /dev/null 2>&1
done
mdconfig -d -u ${mdstart}
rm -f $D
