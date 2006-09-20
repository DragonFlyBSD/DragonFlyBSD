#
# $DragonFly: src/usr.sbin/named-checkconf/Makefile,v 1.3 2006/09/20 21:48:39 victor Exp $

USE_LIBBIND=	yes
USE_LIBBIND9=	yes
USE_LIBISC=	yes
.include "${.CURDIR}/../../usr.sbin/named/Makefile.inc"

.PATH:	${BIND_DIR}/bin/check

PROG=	named-checkconf
SRCS=	named-checkconf.c check-tool.c
MAN=	named-checkconf.8

CFLAGS+= -I${.CURDIR}/../../usr.sbin/named \
	-I${.CURDIR}/../../usr.sbin/named/include \
	-I${BIND_DIR}/lib/bind9/include \
	-I${BIND_DIR}/bin/check \
	-DNAMED_CONFFILE=\"${DESTETC}/named.conf\"

.include <bsd.prog.mk>
