#!/bin/sh -x

# $Id: build_installer_packages.sh,v 1.32 2005/04/06 20:56:23 cpressey Exp $
# Build packages for BSD Installer components.
# This script generally requires root privledges.
# copy_ports_to_portsdir.sh should generally be run first.

SCRIPT=`realpath $0`
SCRIPTDIR=`dirname $SCRIPT`

[ -r $SCRIPTDIR/build.conf ] && . $SCRIPTDIR/build.conf
. $SCRIPTDIR/build.conf.defaults
. $SCRIPTDIR/pver.conf

PVERSUFFIX=""
if [ "X$RELEASEBUILD" != "XYES" ]; then
	PVERSUFFIX=.`date "+%Y.%m%d"`
fi

WITH_NLS_DEF=""
if [ "X$WITH_NLS" = "XYES" ]; then
	WITH_NLS_DEF="WITH_NLS=YES"
fi

rebuild_port()
{
	cd $PORTSDIR/$1/$2/			&& \
	rm -rf work distinfo			&& \
	make makesum				&& \
	make patch				&& \
	chmod -R 777 work			&& \
	make $WITH_NLS_DEF package		&& \
	rm -rf work
}

pkg_delete -f 'libaura-*'
pkg_delete -f 'libinstaller-*'
pkg_delete -f '*dfui*'
pkg_delete -f 'thttpd-notimeout-*'
pkg_delete -f 'lua50-*'
if [ "X$REMOVEOLDPKGS" = "XYES" ]; then
	rm -rf $PACKAGESDIR/libaura-*.????.????.t?z
	rm -rf $PACKAGESDIR/libinstaller-*.????.????.t?z
	rm -rf $PACKAGESDIR/*dfui*.????.????.t?z
	rm -rf $PACKAGESDIR/lua50-*.????.????.t?z
fi

# Now, rebuild all the ports, making packages in the process.

rebuild_port devel libaura			&& \
rebuild_port sysutils libdfui			&& \
rebuild_port sysutils libinstaller		&& \
rebuild_port sysutils dfuibe_installer		&& \
rebuild_port sysutils dfuife_curses		&& \
rebuild_port sysutils dfuife_cgi		&& \
rebuild_port www thttpd-notimeout		&& \
if [ "X$INSTALL_DFUIFE_QT" = "XYES" ]; then
	rebuild_port sysutils dfuife_qt
fi						&& \
if [ "X$INSTALL_DFUIBE_LUA" = "XYES" ]; then
	rebuild_port lang lua50
	rebuild_port devel lua50-compat51
	rebuild_port devel lua50-posix
	rebuild_port devel lua50-pty
	rebuild_port devel lua50-gettext
	rebuild_port devel lua50-dfui
	rebuild_port devel lua50-filename
	rebuild_port devel lua50-app
	rebuild_port net lua50-socket
	rebuild_port sysutils dfuibe_lua
fi
