# $FreeBSD: src/usr.sbin/pppd/Makefile,v 1.19.2.2 2001/08/19 03:32:43 kris Exp $

CFLAGS+= -DHAVE_PATHS_H

PROG=	pppd
SRCS=	main.c magic.c fsm.c lcp.c ipcp.c upap.c chap.c ccp.c \
	demand.c auth.c options.c sys-bsd.c
MAN=	pppd.8
BINMODE=4550
BINOWN=	root
BINGRP=	dialer
WARNS?=	0

LDADD=	-lcrypt -lutil -lmd
DPADD=	${LIBCRYPT} ${LIBUTIL} ${LIBMD}

# Callback Control Protocol
CFLAGS+=-DCBCP_SUPPORT
SRCS+=	cbcp.c

# Filter support
CFLAGS+=-DPPP_FILTER -I${.CURDIR}/../../contrib/libpcap
LDADD+=	-lpcap
DPADD+=	${LIBPCAP}

# MS-CHAP support.  Requires the DES library.
.if exists(${.CURDIR}/../../secure) && !defined(NO_CRYPT)  && !defined(NO_OPENSSL)
CFLAGS+=-DCHAPMS
SRCS+=	chap_ms.c
LDADD+=	-lcrypto
DPADD+=	${LIBCRYPTO}
.endif

.include <bsd.prog.mk>
