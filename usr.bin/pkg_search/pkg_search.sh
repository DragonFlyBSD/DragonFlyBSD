#!/bin/sh
#
# Copyright (c) 2007 The DragonFly Project.  All rights reserved.
#
# This code is derived from software contributed to The DragonFly Project
# by Matthias Schmidt <schmidtm@mathematik.uni-marburg.de>, University of
# Marburg.
#
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without 
# modification, are permitted provided that the following conditions are met:
#
# - Redistributions of source code must retain the above copyright notice, 
#   this list of conditions and the following disclaimer.
# - Redistributions in binary form must reproduce the above copyright notice, 
#   this list of conditions and the following disclaimer in the documentation 
#   and/or other materials provided with the distribution.
# - Neither the name of The DragonFly Project nor the names of its
#   contributors may be used to endorse or promote products derived
#   from this software without specific, prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# $DragonFly: src/usr.bin/pkg_search/pkg_search.sh,v 1.4 2007/12/27 11:52:10 matthias Exp $

UNAME=`uname -s`
VERSION=`uname -r | cut -d '.' -f 1,2`
SIMPLE=0

if [ $UNAME = "DragonFly" ]; then
	PORTSDIR=/usr/pkgsrc
	PKGSUM=${PORTSDIR}/pkg_summary
	PKGSRCBOX1=http://pkgbox.dragonflybsd.org/packages/${UNAME}-${VERSION}/i386/
	PKGSRCBOX2=http://pkgbox.dragonflybsd.org/packages/DragonFly-1.10.1/i386/
	INDEXFILE=INDEX
	if [ ! -f ${PKGSUM} ]; then
		echo "No pkgsrc(7) tree found.  Fetching pkg_summary(5) file."
		FETCHPATH=${PKGSRCBOX1}/All/pkg_summary.bz2
		mkdir -p ${PORTSDIR}
		fetch -o ${PKGSUM}.bz2 ${FETCHPATH}
		if [ $? -ne 0 ]; then
			FETCHPATH=${PKGSRCBOX2}/All/pkg_summary.bz2
			fetch -o ${PKGSUM}.bz2 ${FETCHPATH}
		fi
		if [ $? -ne 0 ]; then
			echo "Unable to fetch pkg_summary"
			exit 1
		fi
		bunzip2 < ${PKGSUM}.bz2 > ${PKGSUM}
		rm -f ${PKGSUM}.bz2
		SIMPLE=1
	fi
	if [ -e ${PKGSUM} -a ! -e ${PORTSDIR}/${INDEXFILE} ]; then
		SIMPLE=1
	fi
fi

if [ -z $1 ]; then 
        echo "usage: $0 [-k | -v] package"
        exit 1  
fi


case "$1" in
	-k)
		if [ ${SIMPLE} -eq 1 ]; then
			grep "PKGNAME=$2" ${PKGSUM} | cut -d '=' -f 2
			exit 1
		fi
		awk -F\| -v name="$2" \
	    '{\
		    if ($1 ~ name || $4 ~ name || $10 ~ name) { \
			    split($2, a, "/"); \
			    printf("%-20s\t%-25s\n", $1, $4); \
		    }
	    }' ${PORTSDIR}/${INDEXFILE}

	;;
	-v)
		if [ ${SIMPLE} -eq 1 ]; then
			grep "PKGNAME=$2" ${PKGSUM} | cut -d '=' -f 2
			exit 1
		fi
		awk -F\| -v name="$2" \
	    '{\
		    if ($1 ~ name) { \
			    split($2, a, "/"); \
			    printf("Name\t: %s-50\nDir\t: %-50s\nDesc\t: %-50s\nURL\t: %-50s\nDeps\t: %s\n\n", $1, $2, $4, $10, $9); \
		    }
	    }' ${PORTSDIR}/${INDEXFILE}
	;;
	*)
		if [ ${SIMPLE} -eq 1 ]; then
			grep "PKGNAME=$1" ${PKGSUM} | cut -d '=' -f 2
			exit 1
		fi

		awk -F\| -v name="$1" \
	    '{\
		    if ($1 ~ name) { \
			    split($2, a, "/"); \
			    printf("%-20s\t%-25s\n", $1, $4); \
		    }
	    }' ${PORTSDIR}/${INDEXFILE}
	;;
esac

exit $?
