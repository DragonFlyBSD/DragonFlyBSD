#!/bin/sh
#
# $FreeBSD: src/release/scripts/compat4x-install.sh,v 1.1.2.1 2002/05/01 09:51:23 matusita Exp $
# $DragonFly: src/release/scripts/Attic/compat4x-install.sh,v 1.2 2003/06/17 04:27:21 dillon Exp $
#

if [ "`id -u`" != "0" ]; then
	echo "Sorry, this must be done as root."
	exit 1
fi
cat compat4x.?? | tar --unlink -xpzf - -C ${DESTDIR:-/}
exit 0
