#
# $DragonFly: src/usr.sbin/named-checkconf/Makefile,v 1.2 2004/06/26 17:06:35 dillon Exp $

USE_LIBBIND=	yes
USE_LIBISC=	yes
.include "${.CURDIR}/../../usr.sbin/named/Makefile.inc"

.PATH:	${BIND_DIR}/bin/check

PROG=	named-checkconf
SRCS=	named-checkconf.c check-tool.c
MAN=	named-checkconf.8

CFLAGS+= -I${.CURDIR}/../../usr.sbin/named \
	-I${.CURDIR}/../../usr.sbin/named/include \
	-I${BIND_DIR}/bin/check \
	-DNAMED_CONFFILE=\"${DESTETC}/named.conf\"

.include <bsd.prog.mk>
