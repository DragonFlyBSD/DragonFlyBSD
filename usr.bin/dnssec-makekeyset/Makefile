#
# $DragonFly: src/usr.bin/dnssec-makekeyset/Attic/Makefile,v 1.1 2004/05/27 18:15:41 dillon Exp $

USE_LIBBIND=	yes
USE_LIBISC=	yes
.include "${.CURDIR}/../../usr.sbin/named/Makefile.inc"

.PATH:	${BIND_DIR}/bin/dnssec

PROG=	dnssec-makekeyset
SRCS=	dnssec-makekeyset.c dnssectool.c
MAN=	dnssec-makekeyset.8

CFLAGS+= -I${.CURDIR}/../../usr.sbin/named

.include <bsd.prog.mk>
