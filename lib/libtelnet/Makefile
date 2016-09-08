#	From: @(#)Makefile	8.2 (Berkeley) 12/15/93
# $FreeBSD: src/lib/libtelnet/Makefile,v 1.25 2009/02/26 21:43:15 ed Exp $

LIB=		telnet

INTERNALLIB=

SRCS=		genget.c getent.c misc.c

WARNS?=		2

.if !defined(NO_LIBRESSL)
SRCS+=		encrypt.c auth.c enc_des.c sra.c pk.c
CFLAGS+=	-DENCRYPTION -DAUTHENTICATION -DSRA
CFLAGS+=	-I${.CURDIR}/../../crypto/libressl/include
.endif

.include <bsd.lib.mk>
