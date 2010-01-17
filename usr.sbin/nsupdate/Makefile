# $FreeBSD: src/usr.bin/dig/Makefile,v 1.7 1999/11/30 06:25:16 peter Exp $
# $DragonFly: src/usr.sbin/nsupdate/Makefile,v 1.5 2006/09/20 21:48:39 victor Exp $

USE_LIBBIND=	yes
USE_LIBBIND9=	yes
USE_LIBISC=	yes
.include "${.CURDIR}/../../usr.sbin/named/Makefile.inc"

.PATH:	${BIND_DIR}/bin/nsupdate
.PATH:	${BIND_DIR}/doc/man

PROG=	nsupdate
SRCS=	nsupdate.c

CFLAGS+= -I${.CURDIR}/../../usr.sbin/named \
	-I${.CURDIR}/../../usr.sbin/named/include \
	-I${BIND_DIR}/lib/bind9/include 

.if !defined(NO_CRYPT) && !defined(NO_OPENSSL) && !defined(NO_BIND_DNSSEC)
DPADD+=         ${LIBCRYPTO}
LDADD+=         -lcrypto
.endif

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

WARNS?=1

MAN= nsupdate.1

.include <bsd.prog.mk>
