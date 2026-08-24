#!/bin/sh -
#
# Copyright (c) 1991, 1993
#	The Regents of the University of California.  All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. Neither the name of the University nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
#	@(#)mkdep.gcc.sh	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.bin/mkdep/mkdep.gcc.sh,v 1.18 1999/08/28 01:04:04 peter Exp $

D=.depend			# default dependency file is .depend
append=
pflag_sed=

while : ; do
	case "$1" in
	# -a appends to the depend file
	-a)
		append=yes
		shift ;;

	# -f allows you to select a makefile name
	-f)
		D=$2
		shift; shift ;;

	# the -p flag produces "program: program.c" style dependencies
	# so .o's don't get produced
	-p)
		pflag_sed='s;\.o:;:;'
		shift ;;

	*)
		break ;;
	esac
done

if [ $# -eq 0 ]; then
	echo 'usage: mkdep [-ap] [-f file] [flags] file ...' >&2
	exit 1
fi

TMP=_mkdep$$
trap 'rm -f $TMP ; trap 2 ; kill -2 $$' 1 2 3 13 15
trap 'rm -f $TMP' 0

# For C sources, mkdep must use exactly the same cpp and predefined flags
# as the compiler would.  This is easily arranged by letting the compiler
# pick the cpp.  mkdep must be told the cpp to use for exceptional cases.
CC=${CC-"cc"}
MKDEP_CPP=${MKDEP_CPP-"${CC} -E"}
MKDEP_CPP_OPTS=${MKDEP_CPP_OPTS-"-M"};

echo "# $@" > $TMP	# store arguments for debugging

if ! $MKDEP_CPP $MKDEP_CPP_OPTS "$@" >> $TMP; then
	echo 'mkdep: compile failed' >&2
	exit 1
fi

if [ "$append" = "" ]; then
	sed -e "$pflag_sed" -e 's; \./; ;g' < $TMP > $D
else
	sed -e "$pflag_sed" -e 's; \./; ;g' < $TMP >> $D
fi

exit $?
