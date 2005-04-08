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
# $DragonFly: src/sys/conf/newvers.sh,v 1.12 2005/04/08 06:12:11 dillon Exp $

tag="\$Name:  $"

# Extract the tag name, if any.
#
BRANCH=$(echo $tag | awk '{ print $2; }')

# Remove any DragonFly_ prefix or _Slip suffix.  Release branches are 
# typically extracted using a slip tag rather then the branch tag in order
# to guarentee that the subversion file is synchronized with the files being
# extracted.
#
BRANCH=${BRANCH#DragonFly_}
BRANCH=${BRANCH%_Slip}

# This case occurs if we have checked out without a tag (i.e. HEAD), either
# implicitly or explicitly.
#
if [ "X${BRANCH}" = "X$" ]; then
    BRANCH="DEVELOPMENT"
fi
if [ "X${BRANCH}" = "XHEAD" ]; then
    BRANCH="DEVELOPMENT"
fi

# This case occurs if the $Name:  $ field has not been expanded.
#
if [ "X${BRANCH}" = "X" ]; then
    BRANCH="UNKNOWN"
fi

TYPE="DragonFly"

# The SHORTTAG is inclusive of the BLAH_X_Y version (if any)
#
SHORTTAG=${BRANCH}

# Figure out the revision and subversion, if any.  If the tag is in 
# the form NAME_X_Y the revision is extracted from X and Y and the branch
# tag is truncated to just NAME.  Otherwise we are on the HEAD branch and
# we are either HEAD or PREVIEW and the programmed revision is used.
#
# If we are on a branch-tag we must also figure out the sub-version.  The
# sub-version is extracted from the 'subvers-${SHORTTAG}' file.  This file
# typically only exists within branches.
#
REVISION=${BRANCH#*_}
BRANCH=${BRANCH%%_*}

if [ "${REVISION}" != "${BRANCH}" ]; then
    REVISION=$(echo $REVISION | sed -e 's/_/./g')
    for chkdir in machine/../../.. .. ../.. ../../.. /usr/src; do
	if [ -f ${chkdir}/sys/conf/subvers-${SHORTTAG} ]; then
	    SUBVER=$(tail -1 ${chkdir}/sys/conf/subvers-${SHORTTAG} | awk '{ print $1; }')
	    if [ "X${SUBVER}" != "X" ]; then
		REVISION="${REVISION}.${SUBVER}"
		break
	    fi
	fi
    done
else
    # Applies to HEAD branch or tags on the HEAD branch only, ignored when
    # run on a branch.
    #
    REVISION="1.3"
fi

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
