# $FreeBSD: src/usr.bin/dig/Makefile,v 1.7 1999/11/30 06:25:16 peter Exp $
# $DragonFly: src/usr.sbin/nsupdate/Makefile,v 1.3 2004/05/27 18:15:43 dillon Exp $

USE_LIBBIND=	yes
USE_LIBISC=	yes
.include "${.CURDIR}/../../usr.sbin/named/Makefile.inc"

.PATH:	${BIND_DIR}/bin/nsupdate
.PATH:	${BIND_DIR}/doc/man

PROG=	nsupdate
SRCS=	nsupdate.c

CFLAGS+= -I${.CURDIR}/../../usr.sbin/named
MAN= nsupdate.8

.include <bsd.prog.mk>
