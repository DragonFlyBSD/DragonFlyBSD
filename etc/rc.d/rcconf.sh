#!/bin/sh
#
# $FreeBSD: src/etc/rc.d/rcconf.sh,v 1.2 2003/01/25 20:02:35 mtm Exp $
# $DragonFly: src/etc/rc.d/rcconf.sh,v 1.2 2003/12/11 23:28:41 dillon Exp $
#

# PROVIDE: rcconf
# REQUIRE: initdiskless
# BEFORE:  disks initrandom
# KEYWORD: DragonFly FreeBSD

. /etc/rc.subr
dummy_rc_command "$1"

echo "Loading configuration files."
load_rc_config 'XXX'
