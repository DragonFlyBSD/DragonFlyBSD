#
# $DragonFly: src/usr.sbin/rndc-confgen/Makefile,v 1.1 2004/05/27 18:15:43 dillon Exp $

USE_LIBBIND=	yes
USE_LIBISC=	yes
.include "${.CURDIR}/../../usr.sbin/named/Makefile.inc"

.PATH:	${BIND_DIR}/bin/rndc
.PATH:	${BIND_DIR}/bin/rndc/unix

PROG=	rndc-confgen
SRCS=	rndc-confgen.c os.c util.c
MAN=	rndc-confgen.8

CFLAGS+= -I${.CURDIR}/../../usr.sbin/named \
	-I${BIND_DIR}/bin/rndc/include \
	-DRNDC_CONFFILE=\"${DESTETC}/rndc.conf\" \
	-DRNDC_KEYFILE=\"${DESTETC}/rndc.key\"

.include <bsd.prog.mk>
