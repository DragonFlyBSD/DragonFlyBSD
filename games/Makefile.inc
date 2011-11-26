#	@(#)Makefile.inc	8.1 (Berkeley) 5/31/93
# $FreeBSD: src/games/Makefile.inc,v 1.10.2.2 2002/02/18 16:02:39 ru Exp $
# $DragonFly: src/games/Makefile.inc,v 1.3 2006/10/08 16:22:34 pavalos Exp $

BINDIR?=	/usr/games
FILESDIR?=	${SHAREDIR}/games
WARNS?=		6

.if defined(HIDEGAME)
ORIGBINDIR:=	${BINDIR}
BINDIR:=	${BINDIR}/hide
ORIGBINGRP:=	${BINGRP}
BINGRP=		games
BINMODE=	550
.endif
