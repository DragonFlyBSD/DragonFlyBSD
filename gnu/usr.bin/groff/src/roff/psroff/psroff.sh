#! /bin/sh -
#
# $FreeBSD: src/gnu/usr.bin/groff/src/roff/psroff/psroff.sh,v 1.2 2001/07/10 17:23:07 ru Exp $
# $DragonFly: src/gnu/usr.bin/groff/src/roff/psroff/psroff.sh,v 1.3 2005/09/01 20:47:14 asmodai Exp $

exec groff -Tps -l -C ${1+"$@"}
