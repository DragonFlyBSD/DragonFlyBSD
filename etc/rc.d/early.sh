#!/bin/sh
#
# $FreeBSD: src/etc/rc.d/early.sh,v 1.1 2003/04/24 08:27:29 mtm Exp $
# $DragonFly: src/etc/rc.d/Attic/early.sh,v 1.2 2003/12/11 23:28:41 dillon Exp $
#

# PROVIDE: early
# REQUIRE: disks localswap
# BEFORE:  fsck
# KEYWORD: DragonFly FreeBSD

#
# Support for legacy /etc/rc.early script
#
if [ -r /etc/rc.early ]; then
	. /etc/rc.early
fi

. /etc/rc.subr
dummy_rc_command "$1"

