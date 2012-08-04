#!/bin/sh
#
# Copyright (c) 2009-2012
#	The DragonFly Project.  All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in
#    the documentation and/or other materials provided with the
#    distribution.
# 3. Neither the name of The DragonFly Project nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific, prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
# COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
# AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
# OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#

CDIR=$(dirname $0)
CNAME=$(basename $0)

# XXX clang needs some special handling
#
# it is called only for "cc" and "gcc" and even then it could have been
# run on c++ files
#
if [ "${CCVER}" = "clang" ]; then
	if [ "${CNAME}" = "cpp" ]; then
		exec ${CDIR}/../gcc41/cpp "$@"
	elif [ "${CNAME}" = "c++" -o "${CNAME}" = "g++" ]; then
		exec ${CDIR}/../gcc41/c++ "$@"
	elif [ -z $beenhere ]; then
		export beenhere=1
		oldargs="$@"
		export oldargs
		INCOPT="-nobuiltininc -nostdinc \
		    -isysroot @@INCPREFIX@@ \
		    -isystem /usr/include \
		    -isystem /usr/libdata/gcc41 \
		    -isystem /usr/include/c++/4.1"
	elif [ "${CNAME}" = "cc" -o "${CNAME}" = "gcc" ]; then
		exec ${CDIR}/../gcc41/cc $oldargs
	fi
elif [ "${CCVER}" = "clangsvn" ]; then
	if [ "${CNAME}" = "cpp" ]; then
		exec ${CDIR}/../gcc41/cpp "$@"
	else
		INCOPT="-nobuiltininc -nostdinc \
		    -isysroot @@INCPREFIX@@ \
		    -isystem /usr/include \
		    -isystem /usr/include/c++/4.4"
	fi
elif [ "${CCVER}" = "gcc46" ]; then
	GCC46VER=`gnatc++ -dumpversion`
	GCC46MAC=`gnatc++ -dumpmachine`
	INCOPT="-nostdinc \
	    -iprefix @@INCPREFIX@@ \
	    -iwithprefixbefore /usr/include \
	    -isystem /usr/pkg/include/c++/${GCC46VER} \
	    -isystem /usr/pkg/include/c++/${GCC46VER}/${GCC46MAC}"
elif [ "${CCVER}" = "gcc47" ]; then
	GCC47VER=`/usr/pkg/gcc-aux/bin/c++ -dumpversion`
	GCC47MAC=`/usr/pkg/gcc-aux/bin/c++ -dumpmachine`
	INCOPT="-nostdinc \
	    -iprefix @@INCPREFIX@@ \
	    -iwithprefixbefore /usr/include \
	    -isystem /usr/pkg/gcc-aux/include/c++/${GCC47VER} \
	    -isystem /usr/pkg/gcc-aux/include/c++/${GCC47VER}/${GCC47MAC}"
fi

. /etc/defaults/compilers.conf
[ -f /etc/compilers.conf ] && . /etc/compilers.conf

CUSTOM_CC=`eval echo \$\{${CCVER}_CC\}`
CUSTOM_CFLAGS=`eval echo \$\{${CCVER}_CFLAGS\}`
CUSTOM_CXX=`eval echo \$\{${CCVER}_CXX\}`
CUSTOM_CXXFLAGS=`eval echo \$\{${CCVER}_CXXFLAGS\}`
CUSTOM_CPP=`eval echo \$\{${CCVER}_CPP\}`
CUSTOM_CPPFLAGS=`eval echo \$\{${CCVER}_CPPFLAGS\}`
CUSTOM_VERSION=`eval echo \$\{${CCVER}_VERSION\}`

if [ "${CUSTOM_VERSION}" != "" -a "$1" = "-dumpversion" ]; then
	echo ${CUSTOM_VERSION}
elif [ "${CNAME}" = "cc" -o "${CNAME}" = "gcc" ]; then
	exec ${CUSTOM_CC} ${INCOPT} ${CUSTOM_CFLAGS} "$@"
elif [ "${CNAME}" = "c++" -o "${CNAME}" = "g++" ]; then
	exec ${CUSTOM_CXX} ${INCOPT} ${CUSTOM_CXXFLAGS} "$@"
elif [ "${CNAME}" = "cpp" ]; then
	exec ${CUSTOM_CPP} ${INCOPT} ${CUSTOM_CPPFLAGS} "$@"
fi
