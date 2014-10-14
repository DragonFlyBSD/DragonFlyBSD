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

CNAME=$(basename $0)
CCVER3=`echo ${CCVER} | awk '{print substr($0, 1, 3);}'`

if [ "${CCVER3}" = "gcc" ]; then
	INCOPT="-nostdinc \
	    -iprefix @@INCPREFIX@@ \
	    -iwithprefixbefore /usr/include"
fi

. /etc/defaults/compilers.conf
[ -f /etc/compilers.conf ] && . /etc/compilers.conf

CUSTOM_CC=`eval echo \$\{${CCVER}_CC\}`
CUSTOM_CFLAGS=`eval echo \$\{${CCVER}_CFLAGS\}`
CUSTOM_CXX=`eval echo \$\{${CCVER}_CXX\}`
CUSTOM_CXXFLAGS=`eval echo \$\{${CCVER}_CXXFLAGS\}`
CUSTOM_CPP=`eval echo \$\{${CCVER}_CPP\}`
CUSTOM_CPPFLAGS=`eval echo \$\{${CCVER}_CPPFLAGS\}`

if [ "${CNAME}" = "cc" -o "${CNAME}" = "gcc" ]; then
	if [ -z ${CUSTOM_CC} ]; then
		echo "${CCVER}_CC undefined, see compilers.conf(5)"
		exit 1
	fi
	exec ${CUSTOM_CC} ${INCOPT} ${CUSTOM_CFLAGS} "$@"
elif [ "${CNAME}" = "c++" -o "${CNAME}" = "g++" ]; then
	if [ -z ${CUSTOM_CXX} ]; then
		echo "${CCVER}_CXX undefined, see compilers.conf(5)"
		exit 1
	fi
	INCOPT="${INCOPT} -isystem /usr/local/lib/${CCVER}/include/c++"
	CXXMACHINE=`${CUSTOM_CXX} -dumpmachine`
	MDINCOPT="-isystem \
	    /usr/local/lib/${CCVER}/include/c++/${CXXMACHINE}"
	exec ${CUSTOM_CXX} ${INCOPT} ${MDINCOPT} ${CUSTOM_CXXFLAGS} "$@"
elif [ "${CNAME}" = "cpp" ]; then
	if [ -z ${CUSTOM_CPP} ]; then
		echo "${CCVER}_CPP undefined, see compilers.conf(5)"
		exit 1
	fi
	exec ${CUSTOM_CPP} ${INCOPT} ${CUSTOM_CPPFLAGS} "$@"
fi
