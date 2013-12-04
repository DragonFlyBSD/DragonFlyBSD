# $FreeBSD: src/usr.bin/telnet/Makefile,v 1.33 2007/07/05 08:56:45 bz Exp $

PROG=		telnet

SRCS=		commands.c main.c network.c ring.c sys_bsd.c \
		telnet.c terminal.c utilities.c

CFLAGS+=	-DKLUDGELINEMODE -DUSE_TERMIO -DENV_HACK -DOPIE \
		-I${.CURDIR}/../../lib -I${.CURDIR}/../../lib/libtelnet
WARNS?=		2

LIBTELNET=	${.OBJDIR}/../../lib/libtelnet/libtelnet.a

.if !defined(NO_INET6)
CFLAGS+=	-DINET6
.endif

DPADD=		${LIBTERMCAP} ${LIBTELNET}
LDADD=		-ltermcap ${LIBTELNET}

CFLAGS+=	-DIPSEC
DPADD+=		${LIBIPSEC}
LDADD+=		-lipsec

.if !defined(NO_OPENSSL)
SRCS+=		authenc.c
CFLAGS+=	-DENCRYPTION -DAUTHENTICATION
DPADD+=		${LIBCRYPTO} ${LIBCRYPT} ${LIBPAM}
LDADD+=		-lcrypto -lcrypt ${MINUSLPAM}
.endif

.include <bsd.prog.mk>
