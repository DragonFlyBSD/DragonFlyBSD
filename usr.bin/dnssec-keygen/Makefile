#
# $DragonFly: src/usr.bin/dnssec-keygen/Makefile,v 1.2 2004/06/26 17:06:34 dillon Exp $

USE_LIBBIND=	yes
USE_LIBISC=	yes
.include "${.CURDIR}/../../usr.sbin/named/Makefile.inc"

.PATH:	${BIND_DIR}/bin/dnssec

PROG=	dnssec-keygen
SRCS=	dnssec-keygen.c dnssectool.c
MAN=	dnssec-keygen.8

CFLAGS+= -I${.CURDIR}/../../usr.sbin/named \
	-I${.CURDIR}/../../usr.sbin/named/include

.include <bsd.prog.mk>
