# $DragonFly: src/lib/libbind9/Makefile,v 1.1 2006/09/20 21:48:39 victor Exp $

BIND_DIR=${.CURDIR}/../../contrib/bind-9.3
.include "${BIND_DIR}/lib/isc/api"

CFLAGS+= -I${.CURDIR}/../../usr.sbin/named \
	-I${.CURDIR}/../../usr.sbin/named/include \
	-I${BIND_DIR}/lib/bind9/include

.include "${.CURDIR}/../../usr.sbin/named/Makefile.inc"
.include "${BIND_DIR}/lib/bind9/api"

LIB=		bind9

CFLAGS+= -DLIBINTERFACE=${LIBINTERFACE} -DLIBREVISION=${LIBREVISION} -DLIBAGE=${LIBAGE}

.PATH:		${BIND_DIR}/lib/bind9
SRCS+=		check.c version.c getaddresses.c


.include <bsd.lib.mk>

