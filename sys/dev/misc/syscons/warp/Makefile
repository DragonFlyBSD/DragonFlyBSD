# $FreeBSD: src/sys/modules/syscons/warp/Makefile,v 1.3 1999/08/28 00:47:56 peter Exp $
# $DragonFly: src/sys/dev/misc/syscons/warp/Makefile,v 1.2 2003/06/17 04:28:47 dillon Exp $

KMOD=	warp_saver
SRCS= 	warp_saver.c

NOMAN=
CFLAGS+= -I${.CURDIR}/..
CWARNFLAGS= -Wall -pedantic

.include <bsd.kmod.mk>
