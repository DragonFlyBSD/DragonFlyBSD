#
# $DragonFly: src/usr.bin/dnssec-keygen/Makefile,v 1.1 2004/05/27 18:15:41 dillon Exp $

USE_LIBBIND=	yes
USE_LIBISC=	yes
.include "${.CURDIR}/../../usr.sbin/named/Makefile.inc"

.PATH:	${BIND_DIR}/bin/dnssec

PROG=	dnssec-keygen
SRCS=	dnssec-keygen.c dnssectool.c
MAN=	dnssec-keygen.8

CFLAGS+= -I${.CURDIR}/../../usr.sbin/named

.include <bsd.prog.mk>
