#
# $DragonFly: src/usr.sbin/named-checkzone/Makefile,v 1.2 2004/06/26 17:06:36 dillon Exp $

USE_LIBBIND=	yes
USE_LIBISC=	yes
.include "${.CURDIR}/../../usr.sbin/named/Makefile.inc"

.PATH:	${BIND_DIR}/bin/check

PROG=	named-checkzone
SRCS=	named-checkzone.c check-tool.c
MAN=	named-checkzone.8

CFLAGS+= -I${.CURDIR}/../../usr.sbin/named \
	-I${.CURDIR}/../../usr.sbin/named/include \
	-I${BIND_DIR}/bin/check \
	-DNAMED_CONFFILE=\"${DESTETC}/named.conf\"

.include <bsd.prog.mk>
