# Common defines for all of /sys/boot/i386/
#
# $FreeBSD: src/sys/boot/i386/Makefile.inc,v 1.1.2.2 2000/12/28 12:04:04 ps Exp $
# $DragonFly: src/sys/boot/i386/Attic/Makefile.inc,v 1.2 2003/06/17 04:28:18 dillon Exp $

LOADER_ADDRESS?=	0x200000
CFLAGS+=		-mpreferred-stack-boundary=2
