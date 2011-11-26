#!/bin/sh

#
# Copyright (c) 2008 Peter Holm
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

# Test if quotacheck reports actual usage

. ../default.cfg

export tmp=/tmp/$(basename $0).$$
export D=$diskimage

qc() {
	quotacheck -v $1 > $tmp 2>&1
	grep -q fixed $tmp && cat $tmp
}

if [ $# -eq 0 ]; then
	trap "rm -f $D $tmp" 0
	[ `id -u ` -ne 0 ] && echo "Must be root!" && exit 1

	dede $D 1m 50 || exit 1

	mount | grep "${mntpoint}" | grep -q md${mdstart} && umount -f ${mntpoint}
	mdconfig -l | grep -q md${mdstart} &&  mdconfig -d -u ${mdstart}

	mdconfig -a -t vnode -f $D -u ${mdstart}
	bsdlabel -w md${mdstart} auto
	newfs -U  md${mdstart}${part} > /dev/null
	echo "/dev/md${mdstart}${part} ${mntpoint} ufs rw,userquota 2 2" >> /etc/fstab
	mount ${mntpoint}
	mkdir ${mntpoint}/stressX
	chown $testuser ${mntpoint}/stressX
	set `df -ik ${mntpoint} | tail -1 | awk '{print $4,$7}'`
	export KBLOCKS=$1
	export INODES=$2

	export QK=$((KBLOCKS / 2))
	export QI=$((INODES / 2))
	edquota -u -f ${mntpoint} -e ${mntpoint}:$((QK - 50)):$QK:$((QI - 50 )):$QI ${testuser} > /dev/null 2>&1
	quotaon ${mntpoint}

#	quotaoff ${mntpoint};umount ${mntpoint}; mount ${mntpoint};quotaon ${mntpoint}
#	df -i ${mntpoint}
#	repquota   -v ${mntpoint}
	qc            ${mntpoint}
#	repquota   -v ${mntpoint}
#	echo "- Start test -"

	su ${testuser} $0 xxx
	du -k /mnt/stressX

#	quotaoff ${mntpoint};umount ${mntpoint}; mount ${mntpoint};quotaon ${mntpoint}
#	df -i ${mntpoint}
#	repquota   -v ${mntpoint}
	qc            ${mntpoint}
#	repquota   -v ${mntpoint}

	sed -i -e "/md${mdstart}${part}/d" /etc/fstab
	while mount | grep -q ${mntpoint}; do
		umount $([ $((`date '+%s'` % 2)) -eq 0 ] && echo "-f" || echo "") ${mntpoint} > /dev/null 2>&1
	done
	mdconfig -d -u ${mdstart}
	rm -f $D
else
	for i in `jot 20`; do
		dede ${mntpoint}/stressX/d$i 1m 1
	done
fi
