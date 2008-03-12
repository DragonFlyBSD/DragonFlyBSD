#!/bin/sh -x

# $Id: make_installer_image.sh,v 1.3 2005/04/05 10:37:57 den Exp $
# Trivial driver script for the other four scripts.
# Generally requires root privledges.
# Assumes a 'cd /usr/src/nrelease && make realquickrel'
# (or equivalent) has recently been done.
# Can be run multiple times thereafter.

SCRIPT=`realpath $0`
SCRIPTDIR=`dirname $SCRIPT`

[ -r $SCRIPTDIR/build.conf ] && . $SCRIPTDIR/build.conf
. $SCRIPTDIR/build.conf.defaults

su ${LOCALUSER} -c $SCRIPTDIR/create_installer_tarballs.sh && \
$SCRIPTDIR/copy_ports_to_portsdir.sh && \
$SCRIPTDIR/build_installer_packages.sh && \
$SCRIPTDIR/install_installer_packages.sh && \
chown -R ${LOCALUSER} ${CVSDIR}
