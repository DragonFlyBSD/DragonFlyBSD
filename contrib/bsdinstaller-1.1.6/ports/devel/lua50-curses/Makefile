# New ports collection makefile for: 	lua50-curses
# Date created:				10 Feb 2005
# Whom:					Chris Pressey

PORTNAME=	lua50-curses
PORTVERSION=	0.1a
CATEGORIES=	devel
DISTNAME=	lcurses-0.1-devel
MASTER_SITES=	http://mega.ist.utl.pt/~tngd/lua/

LIB_DEPENDS=	lua50.5:${PORTSDIR}/lang/lua50
BUILD_DEPENDS=	${LOCALBASE}/bin/lua50c51:${PORTSDIR}/devel/lua50-compat51
RUN_DEPENDS=	${LOCALBASE}/bin/lua50c51:${PORTSDIR}/devel/lua50-compat51

MAINTAINER=	cpressey@catseye.mine.nu
COMMENT=	Curses binding for Lua 5.0.x

PLIST_FILES=	lib/lua/5.0/lcurses.so

do-install:
	${INSTALL_DATA} ${WRKSRC}/lcurses.so ${PREFIX}/lib/lua/5.0

OSTYPE!=sysctl -n kern.ostype

.if ${OSTYPE} == "FreeBSD"
.include <bsd.port.mk>
.endif

.if ${OSTYPE} == "DragonFly"
.include <bsd.dfport.mk>
.endif
