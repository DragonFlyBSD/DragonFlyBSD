#! /bin/sh -
#
# $FreeBSD: src/gnu/usr.bin/groff/src/roff/psroff/psroff.sh,v 1.1.10.1 2001/04/26 17:08:36 ru Exp $
# $DragonFly: src/gnu/usr.bin/groff/src/roff/psroff/psroff.sh,v 1.2 2003/06/17 04:25:46 dillon Exp $

exec groff -Tps -l -C ${1+"$@"}
