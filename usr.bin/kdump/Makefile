#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.bin/kdump/Makefile,v 1.4.2.2 2002/07/23 09:16:07 ru Exp $
# $DragonFly: src/usr.bin/kdump/Makefile,v 1.3 2004/01/16 07:45:19 dillon Exp $

.PATH: ${.CURDIR}/../ktrace

PROG=		kdump
SRCS=		kdump.c ioctl.c subr.c
CFLAGS+=	-I${.CURDIR}/../ktrace -I${.CURDIR}/../.. -I${.CURDIR}/../../sys

CLEANFILES=	ioctl.c

ioctl.c: mkioctls
	sh ${.CURDIR}/mkioctls ${DESTDIR}/usr/include > ${.TARGET}

.include <bsd.prog.mk>
