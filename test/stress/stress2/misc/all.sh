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

# Run all the scripts in stress2/misc

[ `id -u ` -ne 0 ] && echo "Must be root!" && exit 1

# Start of list			Run	Known problems					Verified

# altbufferflushes.sh		Y
# alternativeFlushPath.sh	Y
# backingstore.sh		Y
# cdevsw.sh			N
# core.sh			N	No problems seen
# crossmp.sh			Y
# crossmp2.sh			N	panic: sx lock still held			20071101
# devfs.sh			Y
# devfs2.sh			Y							20070503
# fpclone.sh			N	No problem seen
# fpclone2.sh			N	No problem seen
# fs.sh				Y
# fullpath.sh			Y							20081212
# fuzz.sh			N							20080413
# inversion.sh			N	Problem not seen lately
# isofs.sh			Y
# kevent.sh			Y	panic: KN_INFLUX set when not suppose to be	20080501
# kevent2.sh			Y
# kevent3.sh			Y
# kevent4.sh			Y
# kevent5.sh			Y
# kinfo.sh			Y
# kinfo2.sh			Y
# libMicro.sh			Y
# lockf.sh			Y	Page fault in nfs_advlock			20080413
# lookup_shared.sh		N	The default, now
# mac.sh			Y
# md.sh				N	Waiting for fix					20071208
# md2.sh			N	Waiting for fix					20071208
# mmap.sh			N	Waiting for fix					20080222
# mount.sh			N	Known problem					20070505
# mount2.sh			Y
# mountro.sh			N	Waiting for commit				20080725
# mountro2.sh			N	Waiting for commit				20080725
# mountro3.sh			N	Waiting for commit				20080725
# msdos.sh			Y
# newfs.sh			Y	Problem not seen lately				20080513
# newfs2.sh			Y
# newfs3.sh			N	panic: lockmgr: locking against myself		20070505
# newfs4.sh			N	Livelock					20080725
# nfs.sh			Y
# nfs2.sh			N	panic: wrong diroffset				20080801
# nfs3.sh			Y
# nfs4.sh			Y
# nfs5.sh			N	Page fault in ufs/ffs/ffs_vfsops.c:1501		20080913
# nfs6.sh			N	Page fault in ffs_fhtovp+0x18			20080913
# nfsrename.sh			Y
# nullfs.sh			N	panic: xdrmbuf_create with NULL mbuf chain	20081122
# pthread.sh			Y	panic: spin lock held too long			20081109
# quota1.sh			Y
# quota10.sh			N	Deadlock					20081212
# quota2.sh			Y
# quota3.sh			Y
# quota4.sh			N	Known backing store problem			20070703
# quota5.sh			Y
# quota6.sh			N	Known problem with snapshots and no disk space
# quota7.sh			Y							20070505
# quota8.sh			Y							20070505
# quota9.sh			N							20070505
# recursiveflushes.sh		Y
# revoke.sh			Y
# snap.sh			N	Waiting for snap3.sh fix
# snap2-1.sh			Y
# snap2.sh			Y
# snap3.sh			N	Reported as kostik033.html
# snap4.sh			Y
# snap5-1.sh			Y
# snap5.sh			Y
# snap6.sh			Y
# snap7.sh			N	Waiting for snap3.sh fix			20070508
# snapbackup.sh			N	WIP
# softupdate.sh			Y
# statfs.sh			Y
# symlink.sh			Y
# syscall.sh			Y
# ucom.sh			N
# umount.sh			Y
# umountf.sh			Y
# umountf2.sh			N	Waiting for commit of fix
# umountf3.sh			N	Deadlock. Waiting for commit of fix		20081212
# umountf4.sh			Y	Page fault in ufs/ufs/ufs_dirhash.c:204		20081003
# unionfs.sh			N 	Page fault					20070503
# unionfs2.sh			N	Reported as cons224				20070504
# unionfs3.sh			N	Page fault in vfs_statfs			20070504

# End of list

list=`sed -n '/^# Start of list/,/^# End of list/p' < $0 | awk '$3 ~ /Y/ {print $2}'`
[ $# -ne 0 ] && list=$*


rm -f /tmp/misc.log
while true; do
# 	Shuffle the list
	list=`perl -e 'print splice(@ARGV,rand(@ARGV),1), " " while @ARGV;' $list`
	for i in $list; do
		./cleanup.sh
		echo "`date '+%Y%m%d %T'` all: $i" | tee /dev/tty >> /tmp/misc.log
		logger "Starting test all: $i"
		./$i
	done
done
