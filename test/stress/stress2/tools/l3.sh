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

# Script to catch possible leaks in vm, malloc and mbufs

i=0
while true; do
   sysctl vm.kvm_free | tail -1 | sed 's/:/,/'
   vmstat -z | grep VNODE: | awk '{print $1 "," $4}' | sed 's/,$//'
   sleep 1
done | awk -F, '
{
# Pairs of "name, value" are passed to this awk script
	name=$1
	size=$2
	if (NF != 2)
		print "Number of fields for ", name, "is ", NF
	if (size == s[name])
		next;
#	print "name, size, old minimum :", name, size, s[name]

	if ((size < s[name]) || (f[name] == 0)) {
#		print "Initial value / new minimum", n[name], s[name], size
		n[name] = 0
		s[name] = size
		f[name] = 1
	}

	if (++n[name] > 120) {
		cmd="date '+%T'"
		cmd | getline t
		close(cmd)
		if  (++w[name] > 10)
			printf "%s \"%s\" may be leaking, size %d\n", t, name, size
		n[name] = 0
		s[name] = size
	}
}'
