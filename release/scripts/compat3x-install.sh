#!/bin/sh
#
# $FreeBSD: src/release/scripts/compat3x-install.sh,v 1.1.2.1 2002/08/08 08:23:53 ru Exp $
# $DragonFly: src/release/scripts/Attic/compat3x-install.sh,v 1.2 2003/06/17 04:27:21 dillon Exp $
#

if [ "`id -u`" != "0" ]; then
	echo "Sorry, this must be done as root."
	exit 1
fi
cat compat3x.?? | tar --unlink -xpzf - -C ${DESTDIR:-/}
exit 0
