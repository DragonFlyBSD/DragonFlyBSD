#
# $DragonFly: src/usr.sbin/rndc/Makefile,v 1.2 2004/06/26 17:06:36 dillon Exp $

USE_LIBBIND=	yes
USE_LIBISC=	yes
.include "${.CURDIR}/../../usr.sbin/named/Makefile.inc"

.PATH:	${BIND_DIR}/bin/rndc
.PATH:	${BIND_DIR}/bin/rndc/unix

PROG=	rndc
SRCS=	rndc.c os.c util.c
MAN=	rndc.8

CFLAGS+= -I${.CURDIR}/../../usr.sbin/named \
	-I${.CURDIR}/../../usr.sbin/named/include \
	-I${BIND_DIR}/bin/rndc/include \
	-DRNDC_CONFFILE=\"${DESTETC}/rndc.conf\" \
	-DRNDC_KEYFILE=\"${DESTETC}/rndc.key\"

.include <bsd.prog.mk>
