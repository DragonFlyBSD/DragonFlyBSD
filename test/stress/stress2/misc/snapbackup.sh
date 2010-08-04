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

# Maintain four snapshot backups of /tmp in /backup/snap?

[ ! -d /backup ] && mkdir /backup

for i in `mount | grep "/backup" | awk '{print $1}'`; do
   num=`echo $i | sed 's/.*md//'`
   false
   while [ $? -ne 0 ]; do
      umount $i
   done
   mdconfig -d -u $num
done
for i in `mdconfig -l | sed 's/md//g'`; do
   [ $i -lt 1 -o $i -gt 4 ] && continue
   mdconfig -d -u $i
done

cd /tmp/.snap
[ -r snap4 ] && rm -f snap4
[ -r snap3 ] && mv snap3 snap4
[ -r snap2 ] && mv snap2 snap3
[ -r snap1 ] && mv snap1 snap2

mksnap_ffs /tmp /tmp/.snap/snap1

for i in `ls /tmp/.snap/snap*`; do
   name=`basename $i`
   num=`echo $name | sed 's/snap//'`
   mdconfig -a -t vnode -o readonly -f /tmp/.snap/snap${num} -u ${num}
   [ ! -d /backup/snap${num} ] && mkdir /backup/snap${num}
   mount -o ro /dev/md${num} /backup/snap${num}
done
