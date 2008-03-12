# New ports collection makefile for: 	lua50
# Date created:				16 Feb 2005
# Whom:					Chris Pressey <cpressey@catseye.mine.nu>

PORTNAME=	lua50
PORTVERSION=	5.0.2
CATEGORIES=	lang
DISTNAME=	lua-${PORTVERSION}
MASTER_SITES=	http://www.lua.org/ftp/ \
		ftp://ftp.tecgraf.puc-rio.br/pub/lua/ \
		ftp://csg.uwaterloo.ca/pub/lhf/lua/ \
		ftp://ftp.ntua.gr/pub/lang/lua/ \
		ftp://ftp.uni-trier.de/pub/languages/lua/ \
		ftp://ftp.gwdg.de/pub/languages/lua/

COMMENT=	Maintenance (5.0.x) version of Lua

USE_REINPLACE=	yes
ALL_TARGET=	all so
INSTALL_TARGET=	install soinstall
INSTALLS_SHLIB=	YES

do-configure:
	${REINPLACE_CMD} \
		-e 's|^\(MYCFLAGS=\).*$$|\1 ${CFLAGS}|' \
		-e 's|^\(LOADLIB=\).*$$|\1 -DUSE_DLOPEN=1|' \
		-e 's|^\(CC=\).*$$|\1 ${CC}|' \
		-e 's|^\(STRIP=\).*$$|\1 ${STRIP_CMD}|' \
		-e 's|^\(INSTALL_ROOT=\).*$$|\1 ${PREFIX}|' \
		-e 's|^\(INSTALL_EXEC=\).*$$|\1 ${INSTALL_PROGRAM}|' \
		-e 's|^\(INSTALL_DATA=\).*$$|\1 ${INSTALL_DATA}|' \
		-e 's|^\(INSTALL_INC=\).*$$|\1 ${PREFIX}/include/lua50|' \
		-e 's|^#\(USERCONF.*READLINE.*\)$$|\1|' \
		-e 's|^\(EXTRA_LIBS=\).*$$|\1 -lm -lreadline -lhistory -lncurses|' \
		-e 's|V=5.0|V=5|' \
		${WRKSRC}/config

.if ${MACHINE_ARCH:L} == "amd64"
CFLAGS+=	-fPIC
.endif

OSTYPE!=sysctl -n kern.ostype

.if ${OSTYPE} == "FreeBSD"
.include <bsd.port.mk>
.endif

.if ${OSTYPE} == "DragonFly"
.include <bsd.dfport.mk>
.endif
