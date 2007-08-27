# $FreeBSD: src/usr.bin/dig/Makefile,v 1.7 1999/11/30 06:25:16 peter Exp $
# $DragonFly: src/usr.bin/host/Makefile,v 1.6 2007/08/27 16:50:54 pavalos Exp $

USE_LIBBIND=	yes
USE_LIBBIND9=	yes
USE_LIBISC=	yes
.include "${.CURDIR}/../../usr.sbin/named/Makefile.inc"

.PATH:	${BIND_DIR}/bin/dig
.PATH:	${BIND_DIR}/doc/man

PROG=	host
SRCS=	host.c dighost.c
WARNS?=	3

CFLAGS+= -I${.CURDIR}/../../usr.sbin/named \
	-I${.CURDIR}/../../usr.sbin/named/include \
	-I${BIND_DIR}/lib/bind9/include \
	-I${BIND_DIR}/bin/dig/include

.include <bsd.prog.mk>
