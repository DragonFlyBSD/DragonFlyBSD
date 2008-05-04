#!/bin/sh
#
# Copyright (c) 2007 The DragonFly Project.  All rights reserved.
#
# This code is derived from software contributed to The DragonFly Project
# by Matthias Schmidt <matthias@dragonflybsd.org>, University of Marburg.
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
# $DragonFly: src/usr.bin/pkg_search/pkg_search.sh,v 1.6.2.1 2008/05/04 17:12:14 swildner Exp $

UNAME=`uname -s`
VERSION=`uname -r | cut -d '.' -f 1,2`
NO_INDEX=0
PORTSDIR=/usr/pkgsrc
PKGSUM=${PORTSDIR}/pkg_summary
PKGSRCBOX1=http://pkgbox.dragonflybsd.org/packages/${UNAME}-${VERSION}/stable/i386/
PKGSRCBOX2=http://pkgbox.dragonflybsd.org/packages/DragonFly-1.10.1/stable/i386/
INDEXFILE=INDEX

if [ ! -f ${PKGSUM} -a ! -e ${PORTSDIR}/${INDEXFILE} ]; then
	echo "No pkgsrc(7) tree found.  Fetching pkg_summary(5) file."
	FETCHPATH=${PKGSRCBOX1}/All/pkg_summary.bz2
	mkdir -p ${PORTSDIR}
	fetch -o ${PKGSUM}.bz2 ${FETCHPATH}
	if [ $? -ne 0 ]; then
		FETCHPATH=${PKGSRCBOX2}/All/pkg_summary.bz2
		fetch -o ${PKGSUM}.bz2 ${FETCHPATH}
	fi
	if [ $? -ne 0 ]; then
		echo "Unable to fetch pkg_summary(5) file."
		exit 1
	fi
	bunzip2 < ${PKGSUM}.bz2 > ${PKGSUM}
	rm -f ${PKGSUM}.bz2
	NO_INDEX=1
fi
if [ -e ${PKGSUM} -a ! -e ${PORTSDIR}/${INDEXFILE} ]; then
	NO_INDEX=1
fi

# Perform simple search in pkg_summary
bin_simple_search()
{
	awk -F= -v name="$1" '{
		if ($1 == "PKGNAME") {
			if (tolower($2) ~ name) {
				printf("%-20s\t", $2);
				found = 1;
			}
			else found = 0;
		}
		if (found == 1 && $1 == "COMMENT") printf("%-25s\n", $2);
	}' ${PKGSUM}
}

# Perform extended search in pkg_summary
bin_ext_search()
{
	awk -F= -v name="$1" '{
		if ($1 == "PKGNAME")
			if (tolower($2) ~ name) {
				printf("\nName\t: %-50s\n", $2);
				found = 1;
			}
			else found = 0;
		
		if (found == 1 && $1 == "COMMENT")
			printf("Desc\t: %-50s\n", $2);
		if (found == 1 && $1 == "PKGPATH")
			printf("Path\t: %-50s\n", $2);
		if (found == 1 && $1 == "HOMEPAGE")
			printf("URL\t: %-50s\n", $2);
	}' ${PKGSUM}
}

# Perform extended search in INDEX
index_v_search()
{
	if [ ${KFLAG} -eq 0 ]; then
		awk -F\| -v name="$1" '{
			if (tolower($1) ~ name) {
				split($2, a, "/");
				printf("Name\t: %s-50\nDir\t: %-50s\nDesc\t: %-50s"\
					"\nURL\t: %-50s\nDeps\t: %s\n\n", $1, $2, 
					$4, $12, $9);
			}
		}' ${PORTSDIR}/${INDEXFILE}
	else
		awk -F\| -v name="$1" '{
			if (tolower($1) ~ name || tolower($4) ~ name ||
			    tolower($12) ~ name) {
				split($2, a, "/");
				printf("Name\t: %s-50\nDir\t: %-50s\nDesc\t: %-50s"\
					"\nURL\t: %-50s\nDeps\t: %s\n\n", $1, $2, 
					$4, $12, $9);
			}
		}' ${PORTSDIR}/${INDEXFILE}
	fi
}

# Perform simple search in INDEX
index_search()
{
	if [ ${KFLAG} -eq 0 ]; then
		awk -F\| -v name="$1" '{
			if (tolower($1) ~ name) {
				split($2, a, "/");
				printf("%-20s\t%-25s\n", $1, $4);
			}
		}' ${PORTSDIR}/${INDEXFILE}
	else
		awk -F\| -v name="$1" '{
			if (tolower($1) ~ name || tolower($4) ~ name ||
			    tolower($12) ~ name) {
				split($2, a, "/");
				printf("%-20s\t%-25s\n", $1, $4);
			}
		}' ${PORTSDIR}/${INDEXFILE}
	fi
}

usage()
{
        echo "usage: $0 [-k | -v] package"
        exit 1  
}

args=`getopt kv $*`

KFLAG=0
VFLAG=0

set -- $args
for i; do
	case "$i" in
	-k)
		KFLAG=1; shift;;
	-v)
		VFLAG=1; shift;;
	--)
		shift; break;;
	esac
done

if [ -z ${1} ]; then
	usage
fi

if [ ${VFLAG} -eq 0 -a ${NO_INDEX} -eq 1 ]; then
	bin_simple_search $1
elif [ ${VFLAG} -eq 1 -a ${NO_INDEX} -eq 1 ]; then
	bin_ext_search $1
elif [ ${VFLAG} -eq 0 -a ${NO_INDEX} -eq 0 ]; then
	index_search $1
elif [ ${VFLAG} -eq 1 -a ${NO_INDEX} -eq 0 ]; then
	index_v_search $1
fi

exit $?
