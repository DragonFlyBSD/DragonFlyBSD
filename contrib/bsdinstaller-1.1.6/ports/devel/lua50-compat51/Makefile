# New ports collection makefile for: 	lua50-compat51
# Date created:				10 Feb 2005
# Whom:					Chris Pressey

PORTNAME=	lua50-compat51
PORTVERSION=	5.1.r2
CATEGORIES=	devel
DISTNAME=	compat-5.1r2
MASTER_SITES=	http://luaforge.net/frs/download.php/402/

LIB_DEPENDS=	lua50.5:${PORTSDIR}/lang/lua50

MAINTAINER=	cpressey@catseye.mine.nu
COMMENT=	Lua 5.1 package compatibility layer for Lua 5.0.x

PLIST_FILES=	share/lua/5.0/compat-5.1.lua		\
		lib/lua/${DISTNAME}/compat-5.1.c	\
		lib/lua/${DISTNAME}/compat-5.1.h	\
		bin/lua50c51

do-build:
	@echo "Nothing to build."

do-install:
	${MKDIR} -p ${PREFIX}/lib/lua/5.0
	${MKDIR} -p ${PREFIX}/lib/lua/${DISTNAME}
	${MKDIR} -p ${PREFIX}/share/lua/5.0
	${INSTALL_DATA} ${WRKSRC}/compat-5.1.lua ${PREFIX}/share/lua/5.0
	${INSTALL_DATA} ${WRKSRC}/compat-5.1.c ${PREFIX}/lib/lua/${DISTNAME}
	${INSTALL_DATA} ${WRKSRC}/compat-5.1.h ${PREFIX}/lib/lua/${DISTNAME}
	${INSTALL_SCRIPT} ${FILESDIR}/lua50c51.sh ${PREFIX}/bin/lua50c51

OSTYPE!=sysctl -n kern.ostype

.if ${OSTYPE} == "FreeBSD"
.include <bsd.port.mk>
.endif

.if ${OSTYPE} == "DragonFly"
.include <bsd.dfport.mk>
.endif
