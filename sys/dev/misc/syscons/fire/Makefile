# $FreeBSD: src/sys/modules/syscons/fire/Makefile,v 1.1.6.1 2000/08/03 01:05:48 peter Exp $
# $DragonFly: src/sys/dev/misc/syscons/fire/Makefile,v 1.2 2003/06/17 04:28:47 dillon Exp $

KMOD=	fire_saver
SRCS= 	fire_saver.c

NOMAN=
CFLAGS+= -I${.CURDIR}/..
CWARNFLAGS= -Wall -pedantic

.include <bsd.kmod.mk>
