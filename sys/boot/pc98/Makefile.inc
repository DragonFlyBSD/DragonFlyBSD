# Common defines for all of /sys/boot/pc98/
#
# $FreeBSD: src/sys/boot/pc98/Makefile.inc,v 1.3 2002/05/14 12:32:59 nyan Exp $
# $DragonFly: src/sys/boot/pc98/Attic/Makefile.inc,v 1.3 2003/11/10 06:08:37 dillon Exp $

LOADER_ADDRESS?=	0x100000
CFLAGS+=		-ffreestanding -mpreferred-stack-boundary=2
