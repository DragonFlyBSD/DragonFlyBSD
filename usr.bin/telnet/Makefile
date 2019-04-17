# $FreeBSD: src/usr.bin/telnet/Makefile,v 1.33 2007/07/05 08:56:45 bz Exp $

PROG=		telnet

SRCS=		authenc.c commands.c main.c network.c ring.c sys_bsd.c \
		telnet.c terminal.c utilities.c

CFLAGS+=	-DKLUDGELINEMODE -DUSE_TERMIO -DENV_HACK \
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

CFLAGS+=	-DENCRYPTION -DAUTHENTICATION
CFLAGS+=	-I${.CURDIR}/../../crypto/libressl/include
DPADD+=		${LIBCRYPTO} ${LIBCRYPT}
LDADD+=		-lprivate_crypto -lcrypt

.if !defined(RESCUE)
# libtelnet requires PAM by default
DPADD+=		${LIBPAM}
LDADD+=		${MINUSLPAM}
.endif

.include <bsd.prog.mk>
