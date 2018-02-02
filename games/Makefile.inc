#	@(#)Makefile.inc	8.1 (Berkeley) 5/31/93
# $FreeBSD: src/games/Makefile.inc,v 1.10.2.2 2002/02/18 16:02:39 ru Exp $

BINDIR?=	/usr/games
FILESDIR?=	${SHAREDIR}/games
WARNS?=		6

# These games write to /var/games/
.if defined(VARGAMES)
BINGRP=		games
BINMODE=	550
.endif

# Handle libcrypto dependency
.if defined(GAMESCRYPTO)
LDADD+=		-lprivate_crypto
DPADD+=		${LIBCRYPTO}
CFLAGS+=	-I${_SHLIBDIRPREFIX}/usr/include/priv
LDFLAGS+=       ${PRIVATELIB_LDFLAGS}
.endif

# Handle libncurses dependency
.if defined(GAMESCURSES)
DPADD+=		${LIBNCURSES}
LDADD+=		-lprivate_ncurses
CFLAGS+=	-I${_SHLIBDIRPREFIX}/usr/include/priv/ncurses
LDFLAGS+=	${PRIVATELIB_LDFLAGS}
.endif
