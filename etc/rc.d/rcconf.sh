#!/bin/sh
#
# $FreeBSD: src/etc/rc.d/rcconf.sh,v 1.2 2003/01/25 20:02:35 mtm Exp $
# $DragonFly: src/etc/rc.d/rcconf.sh,v 1.1 2003/07/24 06:35:37 dillon Exp $
#

# PROVIDE: rcconf
# REQUIRE: initdiskless
# BEFORE:  disks initrandom
# KEYWORD: DragonFly FreeBSD

. /etc/rc.subr

echo "Loading configuration files."
load_rc_config 'XXX'
