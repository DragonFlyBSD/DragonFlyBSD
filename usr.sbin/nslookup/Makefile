# $FreeBSD: src/usr.bin/dig/Makefile,v 1.7 1999/11/30 06:25:16 peter Exp $
# $DragonFly: src/usr.sbin/nslookup/Makefile,v 1.4 2004/05/27 18:15:43 dillon Exp $

USE_LIBBIND=	yes
USE_LIBISC=	yes
.include "${.CURDIR}/../../usr.sbin/named/Makefile.inc"

.PATH:	${BIND_DIR}/bin/dig
.PATH:	${BIND_DIR}/doc/man

PROG=	nslookup
SRCS=	nslookup.c dighost.c
MAN=

CFLAGS+= -I${.CURDIR}/../../usr.sbin/named \
	-I${BIND_DIR}/bin/dig/include

.include <bsd.prog.mk>
