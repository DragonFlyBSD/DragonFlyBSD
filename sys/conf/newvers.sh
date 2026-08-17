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
#	@(#)newvers.sh	8.1 (Berkeley) 4/20/94
# $FreeBSD: src/sys/conf/newvers.sh,v 1.44.2.30 2003/04/04 07:02:46 murray Exp $

LC_ALL=C; export LC_ALL

OSTYPE="DragonFly"

# Set the branch.
# Examples: "DEVELOPMENT_6_5", "RELEASE_6_4"
BRANCH="DEVELOPMENT_6_5"

# The directory where the source resides
SRCDIR=$1
if [ "${SRCDIR}" = "" ]; then
	SRCDIR=$(dirname $0)/../..
fi

if [ "${KERN_IDENT}" = "" ]; then
	echo "ERROR: environment variable KERN_IDENT is missing"
	exit 1
fi

if [ "${PARAMFILE}" = "" ]; then
	PARAMFILE="${SRCDIR}/sys/sys/param.h"
fi
if [ ! -r "${PARAMFILE}" ]; then
	echo "ERROR: cannot read <sys/param.h> at ${PARAMFILE}"
	exit 1
fi

# Figure out the revision and subversion, if any.  If the tag is in
# the form NAME_X_Y the revision is extracted from X and Y and the branch
# tag is truncated to just NAME.  Otherwise we are on the HEAD branch and
# we are either HEAD or PREVIEW and the programmed revision is used.
REVISION=${BRANCH#*_}
BRANCH=${BRANCH%%_*}

if [ "${REVISION}" != "${BRANCH}" ]; then
	REVISION=$(echo $REVISION | sed -e 's/_/./g')
fi

RELEASE="${REVISION}-${BRANCH}"

# obtain git commit name, like "v2.3.2.449.g84e97*"
GITREV=$(sh ${SRCDIR}/tools/gitrev.sh 2>/dev/null || true)
if [ -n "$GITREV" ]; then
	VERSION="${GITREV}-${BRANCH}"
else
	VERSION="${RELEASE}"
fi

RELDATE=$(awk '/^#define[[:space:]]+__DragonFly_version/ {print $3}' ${PARAMFILE})

if [ ! -r version ]; then
	echo 0 > version
fi
v=$(cat version)
[ -n "$v" ] || v=1

u=${USER:-root}
d=$(pwd)
h=${HOSTNAME:-$(hostname)}
t=$(date)

sh ${SRCDIR}/tools/gencopyright.sh ${SRCDIR} > vers.c

cat << EOF >> vers.c
char version[] = "${OSTYPE} ${VERSION} #${v}: ${t}\\n    ${u}@${h}:${d}\\n";
char ostype[] = "${OSTYPE}";
char osrelease[] = "${RELEASE}";
int osreldate = ${RELDATE};
char kern_ident[] = "${KERN_IDENT}";
EOF

if [ "${BRANCH}" = "DEVELOPMENT" ]; then
	SBRANCH=DEV
elif [ "${BRANCH}" = "RELEASE" ]; then
	SBRANCH=REL
else
	SBRANCH=${BRANCH}
fi

stamp=$(date +%Y%m%d)
echo DragonFly-${MACHINE}-${stamp}-${SBRANCH}-${GITREV} > vers.txt

echo $((v + 1)) > version
