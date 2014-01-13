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

# The directory where the source resides
#
SRCDIR=$1
if [ "${SRCDIR}" = "" ]; then
    SRCDIR=$(dirname $0)/../..
fi

# Set the branch
#
BRANCH="DEVELOPMENT_3_7"

TYPE="DragonFly"

# Figure out the revision and subversion, if any.  If the tag is in 
# the form NAME_X_Y the revision is extracted from X and Y and the branch
# tag is truncated to just NAME.  Otherwise we are on the HEAD branch and
# we are either HEAD or PREVIEW and the programmed revision is used.
#
REVISION=${BRANCH#*_}
BRANCH=${BRANCH%%_*}

if [ "${REVISION}" != "${BRANCH}" ]; then
    REVISION=$(echo $REVISION | sed -e 's/_/./g')
fi

# obtain git commit name, like "v2.3.2.449.g84e97*"
GITREV=$(${SRCDIR}/tools/gitrev.sh 2>/dev/null || true)

RELEASE="${REVISION}-${BRANCH}"
VERSION="${TYPE} ${RELEASE}"
[ -n "$GITREV" ] && VERSION="${TYPE} ${GITREV}-${BRANCH}"

if [ "X${PARAMFILE}" != "X" ]; then
	RELDATE=$(awk '/__DragonFly_version.*propagated to newvers/ {print $3}' \
		${PARAMFILE})
else
	RELDATE=$(awk '/__DragonFly_version.*propagated to newvers/ {print $3}' \
		${SRCDIR}/sys/sys/param.h)
fi


year=`date '+%Y'`
# look for copyright template
if [ -r ${SRCDIR}/share/examples/etc/bsd-style-copyright ]; then
    COPYRIGHT=`sed \
	-e "s/\[year\]/$year/" \
	-e 's/\[your name\]\.*/The DragonFly Project/' \
	-e '/\[id for your version control system, if any\]/d' \
	${SRCDIR}/share/examples/etc/bsd-style-copyright`
fi

# no copyright found, use a dummy
if [ X"$COPYRIGHT" = X ]; then
	COPYRIGHT="/*
 * Copyright (c) $year The DragonFly Project
 * All rights reserved.
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
if [ "$v" = "" ]; then
    v=1
fi
i=`make -V KERN_IDENT`
cat << EOF > vers.c
$COPYRIGHT
char version[] = "${VERSION} #${v}: ${t}\\n    ${u}@${h}:${d}\\n";
char ostype[] = "${TYPE}";
char osrelease[] = "${RELEASE}";
int osreldate = ${RELDATE};
char kern_ident[] = "${i}";
EOF

echo `expr ${v} + 1` > version

if [ "${BRANCH}" = "DEVELOPMENT" ]; then
    SBRANCH=DEV
fi
if [ "${BRANCH}" = "RELEASE" ]; then
    SBRANCH=REL
fi

stamp=`date +%Y%m%d`
echo DragonFly-${MACHINE}-${stamp}-${SBRANCH}-${GITREV} > vers.txt
