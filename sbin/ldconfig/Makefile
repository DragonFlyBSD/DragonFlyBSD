# $FreeBSD: src/sbin/ldconfig/Makefile,v 1.15.2.1 2001/04/25 10:58:31 ru Exp $
# $DragonFly: src/sbin/ldconfig/Makefile,v 1.2 2003/06/17 04:27:33 dillon Exp $

PROG=	ldconfig
SRCS=	elfhints.c ldconfig.c shlib.c support.c
LDDIR?= ${.CURDIR}/../../libexec/rtld-aout
CFLAGS+=-I${LDDIR} -DFREEBSD_AOUT
MAN=	ldconfig.8

.PATH: ${LDDIR}

.include <bsd.prog.mk>
