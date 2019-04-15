# $FreeBSD: src/lib/libexpat/Makefile,v 1.9 2010/01/02 09:58:07 ed Exp $

LDNS=		${.CURDIR}/../../contrib/ldns

LIB=		private_ldns
SHLIB_MAJOR=	2
PRIVATELIB=	shpub

NO_STRICT_ALIASING=

SRCS+=		buffer.c dname.c dnssec.c dnssec_verify.c dnssec_zone.c \
		error.c higher.c host2str.c host2wire.c keys.c net.c \
		packet.c parse.c rbtree.c rdata.c resolver.c \
		rr.c rr_functions.c sha1.c sha2.c \
		str2host.c tsig.c util.c wire2host.c zone.c
# not needed in base but included:
SRCS+=		dane.c dnssec_sign.c duration.c radix.c update.c

SRCS+=		b64_ntop.c b64_pton.c

INCSDIR=	${INCLUDEDIR}/priv/ldns
INCS=		common.h net.h util.h
INCS+=		buffer.h \
		dane.h \
		dname.h \
		dnssec.h \
		dnssec_sign.h \
		dnssec_verify.h \
		dnssec_zone.h \
		duration.h \
		error.h \
		higher.h \
		host2str.h \
		host2wire.h \
		keys.h \
		ldns.h \
		packet.h\
		parse.h \
		radix.h \
		rbtree.h \
		rdata.h \
		resolver.h \
		rr.h \
		rr_functions.h \
		sha1.h \
		sha2.h \
		str2host.h \
		tsig.h \
		update.h \
		wire2host.h \
		zone.h

.PATH:		${LDNS} ${LDNS}/compat ${LDNS}/ldns ${.CURDIR}/ldns

CFLAGS+= 	-I${.CURDIR} -I${LDNS}

LDADD=		-lprivate_crypto
DPADD=		${LIBCRYPTO}
CFLAGS+=	-I${.CURDIR}/../../crypto/libressl/include
LDFLAGS+=       -rpath /usr/lib/priv ${PRIVATELIB_LDFLAGS}

WARNS?=		2

.include <bsd.lib.mk>
