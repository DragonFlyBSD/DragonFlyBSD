# $FreeBSD: src/usr.bin/dig/Makefile,v 1.7 1999/11/30 06:25:16 peter Exp $
# $DragonFly: src/usr.bin/host/Makefile,v 1.3 2004/05/27 18:15:42 dillon Exp $

USE_LIBBIND=	yes
USE_LIBISC=	yes
.include "${.CURDIR}/../../usr.sbin/named/Makefile.inc"

.PATH:	${BIND_DIR}/bin/dig
.PATH:	${BIND_DIR}/doc/man

PROG=	host
SRCS=	host.c dighost.c

CFLAGS+= -I${.CURDIR}/../../usr.sbin/named \
	-I${BIND_DIR}/bin/dig/include

.include <bsd.prog.mk>
