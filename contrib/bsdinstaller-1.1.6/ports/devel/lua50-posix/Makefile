# New ports collection makefile for: 	lua50-posix
# Date created:				10 Feb 2005
# Whom:					Chris Pressey

PORTNAME=	lua50-posix
PORTVERSION=	2005.0327
CATEGORIES=	devel
DISTNAME=	lposix
WRKSRC=		work/posix
MASTER_SITES=	http://www.tecgraf.puc-rio.br/~lhf/ftp/lua/

LIB_DEPENDS=	lua50.5:${PORTSDIR}/lang/lua50
BUILD_DEPENDS=	${LOCALBASE}/bin/lua50c51:${PORTSDIR}/devel/lua50-compat51
RUN_DEPENDS=	${LOCALBASE}/bin/lua50c51:${PORTSDIR}/devel/lua50-compat51

MAINTAINER=	cpressey@catseye.mine.nu
COMMENT=	POSIX bindings for Lua 5.0.x

PLIST_FILES=	lib/lua/5.0/lposix.so	\
		share/lua/5.0/posix.lua

do-install:
	${INSTALL_DATA} ${WRKSRC}/lposix.so ${PREFIX}/lib/lua/5.0/
	${INSTALL_DATA} ${WRKSRC}/posix.lua ${PREFIX}/share/lua/5.0/

OSTYPE!=sysctl -n kern.ostype

.if ${OSTYPE} == "FreeBSD"
.include <bsd.port.mk>
.endif

.if ${OSTYPE} == "DragonFly"
.include <bsd.dfport.mk>
.endif

