#!/bin/sh
#
# $FreeBSD: src/etc/rc.d/early.sh,v 1.1 2003/04/24 08:27:29 mtm Exp $
# $DragonFly: src/etc/rc.d/Attic/early.sh,v 1.3 2004/01/26 17:21:15 rob Exp $
#

# PROVIDE: early
# REQUIRE: disks localswap
# BEFORE:  fsck
# KEYWORD: DragonFly

#
# Support for legacy /etc/rc.early script
#
if [ -r /etc/rc.early ]; then
	. /etc/rc.early
fi

. /etc/rc.subr
dummy_rc_command "$1"

