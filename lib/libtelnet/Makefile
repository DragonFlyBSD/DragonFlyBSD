#	From: @(#)Makefile	8.2 (Berkeley) 12/15/93
# $FreeBSD: src/lib/libtelnet/Makefile,v 1.25 2009/02/26 21:43:15 ed Exp $

LIB=		telnet

INTERNALLIB=

SRCS=		auth.c enc_des.c encrypt.c genget.c getent.c misc.c sra.c pk.c

WARNS?=		2

CFLAGS+=	-DENCRYPTION -DAUTHENTICATION -DSRA
CFLAGS+=	-I${.CURDIR}/../../crypto/libressl/include

.include <bsd.lib.mk>
