#!/bin/sh -
#
# Copyright (c) 1984, 1986, 1990, 1993
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
# 3. All advertising materials mentioning features or use of this software
#    must display the following acknowledgement:
#	This product includes software developed by the University of
#	California, Berkeley and its contributors.
# 4. Neither the name of the University nor the names of its contributors
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
#	@(#)newvers.sh	8.1 (Berkeley) 4/20/94
# $FreeBSD: src/sys/conf/newvers.sh,v 1.44.2.30 2003/04/04 07:02:46 murray Exp $
# $DragonFly: src/sys/conf/newvers.sh,v 1.10 2004/10/22 23:43:25 dillon Exp $

tag="\$Name:  $"

# Set BRANCH based on TAG, remove any DragonFly_ prefix to the tag.
# If there is no tag set the branch name to CURRENT. 
#
BRANCH="`echo $tag | awk '{ print $2; }' | sed -e 's/DragonFly_//'`"

# This case occurs if we have checked out without a tag (i.e. HEAD), either
# implicitly or explicitly.
#
if [ "X${BRANCH}" = "X$" ]; then
    BRANCH="CURRENT"
fi
if [ "X${BRANCH}" = "XHEAD" ]; then
    BRANCH="CURRENT"
fi

# This case occurs if the $Name:  $ field has not been expanded.
#
if [ "X${BRANCH}" = "X" ]; then
    BRANCH="UNKNOWN"
fi

TYPE="DragonFly"
REVISION="1.1"
RELEASE="${REVISION}-${BRANCH}"
VERSION="${TYPE} ${RELEASE}"

if [ "X${PARAMFILE}" != "X" ]; then
	RELDATE=$(awk '/__DragonFly_version.*propagated to newvers/ {print $3}' \
		${PARAMFILE})
else
	RELDATE=$(awk '/__DragonFly_version.*propagated to newvers/ {print $3}' \
		$(dirname $0)/../sys/param.h)
fi


b=share/examples/etc/bsd-style-copyright
year=`date '+%Y'`
# look for copyright template
for bsd_copyright in ../$b ../../$b ../../../$b /usr/src/$b /usr/$b
do
	if [ -r "$bsd_copyright" ]; then
		COPYRIGHT=`sed \
		    -e "s/\[year\]/$year/" \
		    -e 's/\[your name here\]\.* /DragonFly Inc./' \
		    -e 's/\[your name\]\.*/DragonFly Inc./' \
		    -e '/\[id for your version control system, if any\]/d' \
		    $bsd_copyright` 
		break
	fi
done

# no copyright found, use a dummy
if [ X"$COPYRIGHT" = X ]; then
	COPYRIGHT="/*
 * Copyright (c) $year
 *	DragonFly Inc. All rights reserved.
 *
 */"
fi

# add newline
COPYRIGHT="$COPYRIGHT
"

LC_ALL=C; export LC_ALL
if [ ! -r version ]
then
	echo 0 > version
fi

touch version
v=`cat version` u=${USER-root} d=`pwd` h=`hostname` t=`date`
i=`make -V KERN_IDENT`
cat << EOF > vers.c
$COPYRIGHT
char sccspad[32 - 4 /* sizeof(sccs) */] = { '\\0' };
char sccs[4] = { '@', '(', '#', ')' };
char version[] = "${VERSION} #${v}: ${t}\\n    ${u}@${h}:${d}\\n";
char ostype[] = "${TYPE}";
char osrelease[] = "${RELEASE}";
int osreldate = ${RELDATE};
char kern_ident[] = "${i}";
EOF

echo `expr ${v} + 1` > version
