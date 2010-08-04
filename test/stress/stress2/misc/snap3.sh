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

# Test with two snapshots
# 20070506 Page fault in g_io_request+0x7f

mount | grep "/dev/md0 on /mnt" > /dev/null && umount /mnt
rm -f /tmp/.snap/pho.1
rm -f /tmp/.snap/pho.2
trap "rm -f /tmp/.snap/pho.?" 0
mount | grep "/mnt" | grep md5 > /dev/null && umount /mnt
mdconfig -l | grep -q md5 &&  mdconfig -d -u 5
mdconfig -l | grep -q md6 &&  mdconfig -d -u 6

for i in `jot 64`; do
   mksnap_ffs /tmp /tmp/.snap/pho.1
   mksnap_ffs /tmp /tmp/.snap/pho.2
   mdconfig -a -t vnode -f /tmp/.snap/pho.1 -u 5 -o readonly
   mdconfig -a -t vnode -f /tmp/.snap/pho.2 -u 6 -o readonly
   mount -o ro /dev/md5 /mnt

   sleep 3

   umount /mnt
   mdconfig -d -u 5
   mdconfig -d -u 6
   rm -f /tmp/.snap/pho.1
   rm -f /tmp/.snap/pho.2
done
