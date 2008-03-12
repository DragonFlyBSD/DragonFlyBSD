# New ports collection makefile for: 	lua50-socket
# Date created:				15 Feb 2005
# Whom:					Chris Pressey

PORTNAME=	lua50-socket
PORTVERSION=	2.0.b3
CATEGORIES=	net
DISTNAME=	luasocket-2.0-beta3
MASTER_SITES=	http://www.cs.princeton.edu/~diego/professional/luasocket/

COMMENT=	Socket binding for Lua 5.0.x (+5.1-compat)

BUILD_DEPENDS=	${LOCALBASE}/bin/lua50c51:${PORTSDIR}/devel/lua50-compat51
RUN_DEPENDS=	${LOCALBASE}/bin/lua50c51:${PORTSDIR}/devel/lua50-compat51

PLIST_FILES=	lib/lua/5.0/lsocket.so		\
		lib/lua/5.0/lmime.so		\
		share/lua/5.0/ftp.lua		\
		share/lua/5.0/http.lua		\
		share/lua/5.0/ltn12.lua		\
		share/lua/5.0/mime.lua		\
		share/lua/5.0/smtp.lua		\
		share/lua/5.0/socket.lua	\
		share/lua/5.0/tp.lua		\
		share/lua/5.0/url.lua

USE_GMAKE=	YES
MAKEFILE=	makefile.Linux

do-install:
	${INSTALL_DATA} ${WRKSRC}/lsocket.so ${PREFIX}/lib/lua/5.0/
	${INSTALL_DATA} ${WRKSRC}/lmime.so ${PREFIX}/lib/lua/5.0/
	${INSTALL_DATA} ${WRKSRC}/lua/*.lua ${PREFIX}/share/lua/5.0/

OSTYPE!=sysctl -n kern.ostype

.if ${OSTYPE} == "FreeBSD"
.include <bsd.port.mk>
.endif

.if ${OSTYPE} == "DragonFly"
.include <bsd.dfport.mk>
.endif
