#	@(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.bin/kdump/Makefile,v 1.12 2006/05/20 14:27:22 netchild Exp $

.PATH: ${.CURDIR}/../ktrace

PROG=		kdump
SRCS=		kdump.c ioctl.c kdump_subr.c subr.c
CFLAGS+=	-I${.CURDIR}/../ktrace -I${.CURDIR} -I${.CURDIR}/../.. -I${.CURDIR}/../../sys
CFLAGS+=	-I${.CURDIR}/../../sys/dev/drm/include
CFLAGS+=	-I${.CURDIR}/../../sys/contrib/dev/acpica/source/include

CLEANFILES=	ioctl.c kdump_subr.c

.if make(test)
SFLAG=		-s
test: all
.endif

ioctl.c!
	sh ${.CURDIR}/mkioctls ${SFLAG} ${DESTDIR}/usr/include ${.CURDIR} > ${.TARGET}

kdump_subr.c!
	sh ${.CURDIR}/mksubr ${DESTDIR}/usr/include > ${.TARGET}

.include <bsd.prog.mk>
