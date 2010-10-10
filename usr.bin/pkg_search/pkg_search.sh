#!/bin/sh
#
# Copyright (c) 2007-09 The DragonFly Project.  All rights reserved.
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

set_binpkg_sites() {
	: ${BINPKG_BASE:=http://mirror-master.dragonflybsd.org/packages}
	: ${BINPKG_SITES:=$BINPKG_BASE/$cpuver/DragonFly-$osver/stable}
}

UNAME=`uname -s`
osver=`uname -r | awk -F - '{ print $1; }'`
cpuver=`uname -p | awk -F - '{ print $1; }'`
[ -f /etc/settings.conf ] && . /etc/settings.conf
set_binpkg_sites

NO_INDEX=0
PORTSDIR=/usr/pkgsrc
PKGSUM=${PORTSDIR}/pkg_summary
INDEXFILE=INDEX

# Download the pkg_summary file
download_summary()
{
	echo "Fetching pkg_summary(5) file."
	for tries in 1 2
	do
		FETCHPATH=${BINPKG_SITES}/All/pkg_summary.bz2
		fetch -o ${PKGSUM}.bz2 ${FETCHPATH}
		[ $? -eq 0 ] && break

		# retry with default
		unset BINPKG_BASE
		unset BINPKG_SITES
		set_binpkg_sites
	done
	if [ $? -ne 0 ]; then
		echo "Unable to fetch pkg_summary(5) file."
		exit 1
	fi
	bunzip2 < ${PKGSUM}.bz2 > ${PKGSUM}
	rm -f ${PKGSUM}.bz2
}

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
				printf("Name\t: %s-50\nDir\t: %-50s\nDesc\t: %-50s"\
					"\nURL\t: %-50s\nDeps\t: %s\n\n", $1, $2,
					$4, $12, $9);
			}
		}' ${PORTSDIR}/${INDEXFILE}
	else
		awk -F\| -v name="$1" '{
			if (tolower($1) ~ name || tolower($4) ~ name ||
			    tolower($12) ~ name) {
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
				printf("%-20s\t%-25s\n", $1, $4);
			}
		}' ${PORTSDIR}/${INDEXFILE}
	else
		awk -F\| -v name="$1" '{
			if (tolower($1) ~ name || tolower($4) ~ name ||
			    tolower($12) ~ name) {
				printf("%-20s\t%-25s\n", $1, $4);
			}
		}' ${PORTSDIR}/${INDEXFILE}
	fi
}

show_description()
{
	PDESC=`awk -F\| -v name="$1" '{
		if (tolower($1) == name) {
			split($2, ppath, "/");
			printf("%s\n", $5);
		}
	}' ${PORTSDIR}/${INDEXFILE}`
	if [ -f "${PORTSDIR}/${PDESC}" ]; then
		cat "${PORTSDIR}/${PDESC}"
	else
		echo "Unable to locate package $1.  Please provide the exact"
		echo "package name as given by pkg_search(1).  You need the"
		echo "complete pkgsrc(7) tree to perform this operation."
	fi
}

usage()
{
        echo "usage: `basename $0` [-kv] package"
        echo "       `basename $0` -s package"
        echo "       `basename $0` -d"
        exit 1
}

if [ ! -f ${PKGSUM} -a ! -e ${PORTSDIR}/${INDEXFILE} ]; then
	echo "No pkgsrc(7) tree found."
	mkdir -p ${PORTSDIR}
	download_summary
	NO_INDEX=1
fi
if [ -e ${PKGSUM} -a ! -e ${PORTSDIR}/${INDEXFILE} ]; then
	NO_INDEX=1
fi

args=`getopt dksv $*`

SFLAG=0
KFLAG=0
VFLAG=0
DFLAG=0

set -- $args
for i; do
	case "$i" in
	-d)
		DFLAG=1; shift;;
	-k)
		KFLAG=1; shift;;
	-s)
		SFLAG=1; shift;;
	-v)
		VFLAG=1; shift;;
	--)
		shift; break;;
	esac
done

if [ ${DFLAG} -eq 0 -a -z ${1} ]; then
	usage
fi

if [ ${DFLAG} -eq 1 ]; then
	download_summary
	exit $?
fi

if [ ${VFLAG} -eq 0 -a ${NO_INDEX} -eq 1 ]; then
	bin_simple_search $1
elif [ ${VFLAG} -eq 1 -a ${NO_INDEX} -eq 1 ]; then
	bin_ext_search $1
elif [ ${SFLAG} -eq 1 -a ${NO_INDEX} -eq 0 ]; then
	show_description $1
elif [ ${VFLAG} -eq 0 -a ${NO_INDEX} -eq 0 ]; then
	index_search $1
elif [ ${VFLAG} -eq 1 -a ${NO_INDEX} -eq 0 ]; then
	index_v_search $1
fi

exit $?
