#
# $DragonFly: src/usr.sbin/rndc/Makefile,v 1.3 2006/09/20 21:48:39 victor Exp $

USE_LIBBIND=	yes
USE_LIBBIND9=	yes
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
	-I${BIND_DIR}/lib/bind9/include \
	-DRNDC_CONFFILE=\"${DESTETC}/rndc.conf\" \
	-DRNDC_KEYFILE=\"${DESTETC}/rndc.key\"

.include <bsd.prog.mk>
