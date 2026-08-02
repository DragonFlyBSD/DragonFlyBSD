#!/bin/sh

case $1 in
-h | '')
	echo "usage: $0 <src_dir>"
	exit 1
	;;
esac
SRCDIR=$1

LC_ALL=C; export LC_ALL

if [ -r "${SRCDIR}/COPYRIGHT" ]; then
	year=$(sed -nE -e 's/^Copyright .* 2003-([0-9]*) The DragonFly Project.*$/\1/p' \
		${SRCDIR}/COPYRIGHT)
else
	year=$(date '+%Y')
fi

cat << EOF
/*-
 * Copyright (c) 2003-$year The DragonFly Project
 * All rights reserved.
 */

EOF
