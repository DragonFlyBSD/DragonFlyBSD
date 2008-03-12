#!/bin/sh -x

# $Id: remove_installer_packages.sh,v 1.3 2005/04/08 07:02:24 cpressey Exp $
# Remove all BSD Installer packages from the running system.
# Note that this generally requires root privledges.

SCRIPT=`realpath $0`
SCRIPTDIR=`dirname $SCRIPT`

[ -r $SCRIPTDIR/build.conf ] && . $SCRIPTDIR/build.conf
. $SCRIPTDIR/build.conf.defaults
. $SCRIPTDIR/pver.conf

PVERSUFFIX=""
if [ "X$RELEASEBUILD" != "XYES" ]; then
	PVERSUFFIX=.`date "+%Y.%m%d"`
fi

INSTALLER_PACKAGES='libaura-*
		    libdfui-*
		    libinstaller-*
		    dfuibe_*
		    dfuife_*
		    thttpd-notimeout-*
		    lua50-*'

for PKG in $INSTALLER_PACKAGES; do
	pkg_delete -f $PKG || true
done

