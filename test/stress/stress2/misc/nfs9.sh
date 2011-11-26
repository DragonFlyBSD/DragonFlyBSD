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

# Copy of nfs4.sh, where it was discovered that a missing killall.sh script
# turned up quite a few new panics. For example:

# vfs_mount_destroy: nonzero writeopcount
# Lock nfs not locked @ kern/vfs_default.c:462
# Assertion x == LK_SHARERS_LOCK(1) failed at kern/kern_lock.c:236

[ `id -u ` -ne 0 ] && echo "Must be root!" && exit 1

. ../default.cfg

[ ! -d $mntpoint ] &&  mkdir $mntpoint
for i in `jot 10`; do
	mount | grep "$mntpoint" | grep nfs > /dev/null && umount $mntpoint
	mount -t nfs -o tcp -o retrycnt=3 -o intr -o soft -o rw 127.0.0.1:/tmp $mntpoint
	rm -rf $mntpoint/stressX/*
	rm -rf /tmp/stressX.control

	export RUNDIR=$mntpoint/nfs/stressX
	[ ! -d $RUNDIR ] && mkdir -p $RUNDIR
	export runRUNTIME=3m
	rm -rf /tmp/stressX.control/*

	(cd ..; ./run.sh all.cfg) &
	sleep 60

	while mount | grep -q $mntpoint; do
		umount -f $mntpoint > /dev/null 2>&1
	done
	kill -9 $!
done
../tools/killall.sh
