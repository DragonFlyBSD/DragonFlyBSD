# $FreeBSD: src/usr.bin/dig/Makefile,v 1.7 1999/11/30 06:25:16 peter Exp $
# $DragonFly: src/usr.sbin/nsupdate/Makefile,v 1.4 2004/06/26 17:06:36 dillon Exp $

USE_LIBBIND=	yes
USE_LIBISC=	yes
.include "${.CURDIR}/../../usr.sbin/named/Makefile.inc"

.PATH:	${BIND_DIR}/bin/nsupdate
.PATH:	${BIND_DIR}/doc/man

PROG=	nsupdate
SRCS=	nsupdate.c

CFLAGS+= -I${.CURDIR}/../../usr.sbin/named \
	-I${.CURDIR}/../../usr.sbin/named/include

MAN= nsupdate.8

.include <bsd.prog.mk>
