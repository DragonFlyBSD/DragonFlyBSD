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

#

[ `id -u ` -ne 0 ] && echo "Must be root!" && exit 1

. ../default.cfg

mounts=15		# Number of parallel scripts
mdstart=$mdstart	# Use md unit numbers from this point
D=$diskimage

if [ $# -eq 0 ]; then
	# start the parallel tests
	for i in `jot $mounts`; do
		./$0 $i &
		./$0 find &
	done

	for i in `jot $mounts`; do
		wait; wait
	done
else
	if [ $1 = find ]; then
		exec 6< /dev/zero
		exec 7< /dev/zero
		exec 8< /dev/zero
		exec 9< /dev/zero
		for i in `jot 128`; do
			ls -l ${mntpoint}* > /dev/null 2>&1
		done
	else

		# The test: Parallel mount and unmounts
		for i in `jot 128`; do
#			mount -t fdescfs null  ${mntpoint}$1	# This causes mound to complain
			mount_fdescfs null ${mntpoint}$1
			while mount | grep -wq ${mntpoint}$1; do
				umount -f ${mntpoint}$1 > /dev/null 2>&1
			done
		done
	fi
fi
