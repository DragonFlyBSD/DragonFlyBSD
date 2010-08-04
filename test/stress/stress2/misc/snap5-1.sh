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

[ ! -d /mnt2 ] && mkdir /mnt2

trap "rm -f /tmp/.snap/pho" 0
for i in `jot 64`; do
   if mount | grep -q "/dev/md0 on /mnt2"; then
      umount /mnt2 || exit 2
   fi
   if mdconfig -l | grep -q md0; then
      mdconfig -d -u 0 || exit 3
   fi
   rm -f /tmp/.snap/pho

   date '+%T'
   mksnap_ffs /tmp /tmp/.snap/pho || continue
   mdconfig -a -t vnode -f /tmp/.snap/pho -u 0 -o readonly || exit 4
   mount -o ro /dev/md0 /mnt2 || exit 5

   ls -l /mnt2 > /dev/null
   r=`head -c4 /dev/urandom | od -N2 -tu4 | sed -ne '1s/  *$//;1s/.* //p'`
   sleep $(( r % 120 ))
done
mount | grep -q "/dev/md0 on /mnt2" && umount /mnt2
mdconfig -l | grep -q md0 && mdconfig -d -u 0
