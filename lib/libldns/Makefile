# $FreeBSD: src/lib/libexpat/Makefile,v 1.9 2010/01/02 09:58:07 ed Exp $

LDNS=		${.CURDIR}/../../contrib/ldns

LIB=		ldns
SHLIB_MAJOR=	1

SRCS  =		rdata.c util.c rr.c packet.c wire2host.c \
		host2str.c buffer.c str2host.c tsig.c resolver.c \
		net.c host2wire.c dname.c dnssec.c dnssec_verify.c \
		keys.c higher.c rr_functions.c parse.c update.c \
		error.c zone.c dnssec_zone.c dnssec_sign.c rbtree.c \
		sha1.c sha2.c

SRCS+=		b32_ntop.c b32_pton.c b64_ntop.c b64_pton.c

INCSDIR=	${INCLUDEDIR}/ldns
INCS=		common.h ldns.h net.h util.h
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
		packet.h\
		parse.h \
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

WARNS?=		2

.include <bsd.lib.mk>
