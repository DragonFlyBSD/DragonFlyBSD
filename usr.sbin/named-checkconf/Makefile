#
# $DragonFly: src/usr.sbin/named-checkconf/Makefile,v 1.1 2004/05/27 18:15:43 dillon Exp $

USE_LIBBIND=	yes
USE_LIBISC=	yes
.include "${.CURDIR}/../../usr.sbin/named/Makefile.inc"

.PATH:	${BIND_DIR}/bin/check

PROG=	named-checkconf
SRCS=	named-checkconf.c check-tool.c
MAN=	named-checkconf.8

CFLAGS+= -I${.CURDIR}/../../usr.sbin/named \
	-I${BIND_DIR}/bin/check \
	-DNAMED_CONFFILE=\"${DESTETC}/named.conf\"

.include <bsd.prog.mk>
