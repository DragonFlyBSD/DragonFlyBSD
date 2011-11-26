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

# Caused panic: ffs_truncate3

[ `id -u ` -ne 0 ] && echo "Must be root!" && exit 1

. ../default.cfg

ftest () {	# fstype, soft update, disk full
   echo "newfs -O $1 `[ $2 -eq 1 ] && echo \"-U\"` md${mdstart}${part}"
   newfs -O $1 `[ $2 -eq 1 ] && echo "-U"` md${mdstart}${part} > /dev/null
   mount /dev/md${mdstart}${part} ${mntpoint}

   export RUNDIR=${mntpoint}/stressX
   disk=$(($3 + 1))	# 1 or 2
   set `df -ik ${mntpoint} | tail -1 | awk '{print $4,$7}'`
   export KBLOCKS=$(($1 * disk))
   export  INODES=$(($2 * disk))

   for i in `jot 10`; do
      (cd ../testcases/rw;./rw -t 2m -i 20)
   done

   while mount | grep -q ${mntpoint}; do
      umount $([ $((`date '+%s'` % 2)) -eq 0 ] && echo "-f") ${mntpoint} > /dev/null 2>&1
   done
}


mount | grep "${mntpoint}" | grep md${mdstart}${part} > /dev/null && umount ${mntpoint}
mdconfig -l | grep md${mdstart} > /dev/null &&  mdconfig -d -u ${mdstart}

mdconfig -a -t swap -s 20m -u ${mdstart}
bsdlabel -w md${mdstart} auto

ftest 1 0 0	# ufs1
ftest 1 0 1	# ufs1, disk full
ftest 2 0 0	# ufs2
ftest 2 0 1	# ufs2, disk full
ftest 2 1 0	# ufs2 + soft update
ftest 2 1 1	# ufs2 + soft update, disk full

mdconfig -d -u ${mdstart}
