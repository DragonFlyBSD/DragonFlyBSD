#!/bin/sh
#
# $FreeBSD: src/etc/rc.d/rcconf.sh,v 1.2 2003/01/25 20:02:35 mtm Exp $
# $DragonFly: src/etc/rc.d/rcconf.sh,v 1.3 2004/01/27 00:42:45 rob Exp $
#

# PROVIDE: rcconf
# REQUIRE: initdiskless
# BEFORE:  disks initrandom
# KEYWORD: DragonFly 

. /etc/rc.subr
dummy_rc_command "$1"

echo "Loading configuration files."
load_rc_config 'XXX'
