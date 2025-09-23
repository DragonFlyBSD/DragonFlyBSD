#!/bin/sh
#
# Copyright (c) 2009-2014
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

# In order to improve the wrapper script efficiency, pre-determine these
# variables at build time instead of at runtime.
INCPREFIX=@@INCPREFIX@@	# e.g., /
MACHARCH=@@MACHARCH@@	# e.g., x86_64
MACHREL=@@MACHREL@@	# e.g., 6.5

. /etc/defaults/compilers.conf
[ -f /etc/compilers.conf ] && . /etc/compilers.conf


case ${CNAME} in
	gcc)
		eval 'CUSTOM_GCC=${'${CCVER}'_GCC}'
		if [ -n "${CUSTOM_GCC}" ]; then
			COMPILER=${CUSTOM_GCC}
			eval 'INCOPT=${'${CCVER}'_INCOPT}'
		else
			COMPILER=/usr/libexec/gcc80/gcc
			INCOPT=${STD_INCOPT}
		fi
		;;
	g++)
		eval 'CUSTOM_GXX=${'${CCVER}'_GXX}'
		if [ -n "${CUSTOM_GXX}" ]; then
			COMPILER=${CUSTOM_GXX}
			eval 'INCOPT=${'${CCVER}'_INCOPT}'
			eval 'INCOPTCXX=${'${CCVER}'_INCOPTCXX}'
		else
			COMPILER=/usr/libexec/gcc80/g++
			INCOPT=${GCC_INCOPT}
			INCOPTCXX="-isystem /usr/include/c++/8.0"
		fi
		;;
	clang)
		eval 'CUSTOM_CLANG=${'${CCVER}'_CLANG}'
		if [ -n "${CUSTOM_CLANG}" ]; then
			COMPILER=${CUSTOM_CLANG}
			eval 'INCOPT=${'${CCVER}'_INCOPT}'
		else
			COMPILER=/usr/libexec/clangbase/clang
			INCOPT=${STD_INCOPT}
		fi
		;;
	clang++)
		eval 'CUSTOM_CLANGCXX=${'${CCVER}'_CLANGCXX}'
		if [ -n "${CUSTOM_CLANGCXX}" ]; then
			COMPILER=${CUSTOM_CLANGCXX}
			eval 'INCOPT=${'${CCVER}'_INCOPT}'
			eval 'INCOPTCXX=${'${CCVER}'_INCOPTCXX}'
		else
			COMPILER=/usr/libexec/clangbase/clang++
			INCOPT=${CLANG_INCOPT}
			INCOPTCXX="-isystem /usr/include/c++/8.0"
		fi
		;;
	cc)
		eval 'CUSTOM_CC=${'${CCVER}'_CC}'
		if [ -n "${CUSTOM_CC}" ]; then
			COMPILER=${CUSTOM_CC}
			eval 'INCOPT=${'${CCVER}'_INCOPT}'
		else
			echo "${CCVER}_CC undefined, see compilers.conf(5)"
			exit 1
		fi
		;;
	c++|CC)
		eval 'CUSTOM_CXX=${'${CCVER}'_CXX}'
		if [ -n "${CUSTOM_CXX}" ]; then
			COMPILER=${CUSTOM_CXX}
			eval 'INCOPT=${'${CCVER}'_INCOPT}'
			eval 'INCOPTCXX=${'${CCVER}'_INCOPTCXX}'
		else
			echo "${CCVER}_CXX undefined, see compilers.conf(5)"
			exit 1
		fi
		;;
	cpp)
		eval 'CUSTOM_CPP=${'${CCVER}'_CPP}'
		if [ -n "${CUSTOM_CPP}" ]; then
			COMPILER=${CUSTOM_CPP}
			eval 'INCOPT=${'${CCVER}'_INCOPT}'
		else
			echo "${CCVER}_CPP undefined, see compilers.conf(5)"
			exit 1
		fi
		;;
	clang-cpp)
		eval 'CUSTOM_CLANGCPP=${'${CCVER}'_CLANGCPP}'
		if [ -n  "${CUSTOM_CLANGCPP}" ]; then
			COMPILER=${CUSTOM_CLANGCPP}
			eval 'INCOPT=${'${CCVER}'_INCOPT}'
		else
			COMPILER=/usr/libexec/clangbase/clang-cpp
			INCOPT=${CLANG_INCOPT}
		fi
		;;
	gcov)
		eval 'CUSTOM_GCOV=${'${CCVER}'_GCOV}'
		if [ -n  "${CUSTOM_GCOV}" ]; then
			exec ${CUSTOM_GCOV} "$@"
		else
			exec /usr/libexec/gcc80/gcov "$@"
		fi
		;;
	*)
		echo "customcc: unrecognized command '${CNAME}'"
		exit 1
		;;
esac

case ${CNAME} in
	gcc|clang|cc)
		eval 'CUSTOM_CFLAGS=${'${CCVER}'_CFLAGS}'
		exec ${COMPILER} ${INCOPT} ${CUSTOM_CFLAGS} "$@"
		;;
	g++|clang++|c++|CC)
		eval 'CUSTOM_CXXFLAGS=${'${CCVER}'_CXXFLAGS}'
		exec ${COMPILER} ${INCOPT} ${INCOPTCXX} ${CUSTOM_CXXFLAGS} "$@"
		;;
	cpp|clang-cpp)
		eval 'CUSTOM_CPPFLAGS=${'${CCVER}'_CPPFLAGS}'
		exec ${COMPILER} ${INCOPT} ${CUSTOM_CPPFLAGS} "$@"
		;;
	*)
		echo "customcc: unrecognized command '${CNAME}'"
		exit 1
		;;
esac
