# $FreeBSD: src/usr.bin/telnet/Makefile,v 1.33 2007/07/05 08:56:45 bz Exp $

PROG=		telnet

SRCS=		authenc.c commands.c main.c network.c ring.c sys_bsd.c \
		telnet.c terminal.c utilities.c

CFLAGS+=	-DKLUDGELINEMODE -DUSE_TERMIO -DENV_HACK -DOPIE \
		-I${.CURDIR}/../../lib -I${.CURDIR}/../../lib/libtelnet
WARNS?=		2
NO_STRICT_ALIASING=

LIBTELNET=	${.OBJDIR}/../../lib/libtelnet/libtelnet.a

.if !defined(NO_INET6)
CFLAGS+=	-DINET6
.endif

DPADD=		${LIBNCURSES} ${LIBTELNET}
LDADD=		-lprivate_ncurses ${LIBTELNET}
CFLAGS+=	-I${_SHLIBDIRPREFIX}/usr/include/priv/ncurses
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

CFLAGS+=	-DIPSEC
DPADD+=		${LIBIPSEC}
LDADD+=		-lipsec

CFLAGS+=	-DENCRYPTION -DAUTHENTICATION
DPADD+=		${LIBCRYPTO} ${LIBCRYPT} ${LIBPAM}
LDADD+=		-lprivate_crypto -lcrypt ${MINUSLPAM}
CFLAGS+=	-I${.CURDIR}/../../crypto/libressl/include

.include <bsd.prog.mk>
