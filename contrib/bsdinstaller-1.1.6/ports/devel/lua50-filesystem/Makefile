# New ports collection makefile for: 	lua50-filesystem
# Date created:				10 Feb 2005
# Whom:					Chris Pressey

PORTNAME=	lua50-filesystem
PORTVERSION=	1.0
CATEGORIES=	devel
DISTNAME=	luafilesystem-${PORTVERSION}
MASTER_SITES=	http://luaforge.net/frs/download.php/430/

LIB_DEPENDS=	lua50.5:${PORTSDIR}/lang/lua50
BUILD_DEPENDS=	${LOCALBASE}/bin/lua50c51:${PORTSDIR}/devel/lua50-compat51
RUN_DEPENDS=	${LOCALBASE}/bin/lua50c51:${PORTSDIR}/devel/lua50-compat51

MAINTAINER=	cpressey@catseye.mine.nu
COMMENT=	Portable File System routines for Lua 5.0.x

PLIST_FILES=	lib/lua/5.0/liblfs.1.0.so	\
		lib/lua/5.0/lfs.so

ALL_TARGET=	lib

OSTYPE!=sysctl -n kern.ostype

.if ${OSTYPE} == "FreeBSD"
.include <bsd.port.mk>
.endif

.if ${OSTYPE} == "DragonFly"
.include <bsd.dfport.mk>
.endif

