#!/bin/sh -x

# $Id: copy_ports_to_portsdir.sh,v 1.3 2005/04/05 10:37:57 den Exp $
# Copy the ports in our CVS tree to the system-wide ports directory.
# This script generally requires root privledges.
# create_installer_tarballs.sh should generally be run first.

SCRIPT=`realpath $0`
SCRIPTDIR=`dirname $SCRIPT`

[ -r $SCRIPTDIR/build.conf ] && . $SCRIPTDIR/build.conf
. $SCRIPTDIR/build.conf.defaults
. $SCRIPTDIR/pver.conf

PVERSUFFIX=""
if [ "X$RELEASEBUILD" != "XYES" ]; then
	PVERSUFFIX=.`date "+%Y.%m%d"`
fi

cd $CVSDIR/$CVSMODULE/ports		&& \
rm -rf */*/work				&& \
for CATEGORY in *; do
	for PORT in $CATEGORY/*; do
		if [ "X$CATEGORY" != "XCVS" -a "X$PORT" != "X$CATEGORY/CVS" ]; then
			rm -rf $PORTSDIR/$PORT
			cp -Rp $PORT $PORTSDIR/$PORT
			if grep -q '^INTERNAL[[:space:]]*=[[:space:]]*YES[[:space:]]*$' $PORT/Makefile; then
				sed -i '' "s/PORTVERSION=[[:space:]]*\([^[:space:]]*\)[[:space:]]*$/PORTVERSION=\1${PVERSUFFIX}/" \
				    $PORTSDIR/$PORT/Makefile
			fi
		fi
	done
done
