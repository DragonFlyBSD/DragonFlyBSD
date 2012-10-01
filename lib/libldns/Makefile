# $FreeBSD: src/lib/libexpat/Makefile,v 1.9 2010/01/02 09:58:07 ed Exp $

LDNS=		${.CURDIR}/../../contrib/ldns

LIB=		ldns
SHLIB_MAJOR=	1
#SRCS=		xmlparse.c xmlrole.c xmltok.c

SRCS  =		rdata.c util.c rr.c packet.c wire2host.c \
		host2str.c buffer.c str2host.c tsig.c resolver.c \
		net.c host2wire.c dname.c dnssec.c dnssec_verify.c \
		keys.c higher.c rr_functions.c parse.c update.c \
		error.c zone.c dnssec_zone.c dnssec_sign.c rbtree.c \
		sha1.c sha2.c

SRCS+=		b32_ntop.c b32_pton.c b64_ntop.c b64_pton.c

#INCS=		bsdxml.h bsdxml_external.h
#MAN=		libbsdxml.3

.PATH:		${LDNS} ${LDNS}/compat

CFLAGS+= 	-I${.CURDIR} -I${LDNS}
CFLAGS+=	-Wno-unused-but-set-variable
#CLEANFILES=	bsdxml.h bsdxml_external.h

WARNS?=		2

.include <bsd.lib.mk>
