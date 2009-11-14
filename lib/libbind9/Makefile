# $DragonFly: src/lib/libbind9/Makefile,v 1.1 2006/09/20 21:48:39 victor Exp $

BIND_DIR=${.CURDIR}/../../contrib/bind-9.5.2
.include "${BIND_DIR}/lib/isc/api"
CFLAGS+= -I${.CURDIR}/../../usr.sbin/named \
	-I${.CURDIR}/../../usr.sbin/named/include \
	-I${BIND_DIR}/lib/bind9/include

.if ${MACHINE_ARCH} == "i386"
CFLAGS+= -I${BIND_DIR}/lib/isc/x86_32/include
CFLAGS+= -DISC_PLATFORM_USETHREADS
CFLAGS+= -DISC_PLATFORM_USEGCCASM
.endif
.if ${MACHINE_ARCH} == "x86_64"
CFLAGS+= -I${BIND_DIR}/lib/isc/x86_64/include
CFLAGS+= -DISC_PLATFORM_USETHREADS
CFLAGS+= -DISC_PLATFORM_USESTDASM
.endif
.include "${.CURDIR}/../../usr.sbin/named/Makefile.inc"
.include "${BIND_DIR}/lib/bind9/api"

LIB=		bind9

CFLAGS+= -DLIBINTERFACE=${LIBINTERFACE} -DLIBREVISION=${LIBREVISION} -DLIBAGE=${LIBAGE}

.PATH:		${BIND_DIR}/lib/bind9
SRCS+=		check.c version.c getaddresses.c

WARNS?=		1

.include <bsd.lib.mk>

