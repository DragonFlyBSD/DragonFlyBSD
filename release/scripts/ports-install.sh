#!/bin/sh
#
# $FreeBSD: src/release/scripts/ports-install.sh,v 1.2.6.1 2002/08/08 08:23:53 ru Exp $
# $DragonFly: src/release/scripts/Attic/ports-install.sh,v 1.2 2003/06/17 04:27:21 dillon Exp $
#

if [ "`id -u`" != "0" ]; then
	echo "Sorry, this must be done as root."
	exit 1
fi
echo "Extracting ports tarball into ${DESTDIR}/usr"
cat ports.tgz | tar --unlink -xpzf - -C ${DESTDIR}/usr
exit 0
