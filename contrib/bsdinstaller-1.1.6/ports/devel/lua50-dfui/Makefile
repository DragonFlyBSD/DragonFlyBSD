# New ports collection makefile for: 	lua50-dfui
# Date created:				21 Feb 2005
# Whom:					Chris Pressey

PORTNAME=	lua50-dfui
PORTVERSION=	0.1
INTERNAL=	YES
CATEGORIES=	devel
DISTNAME=	luadfui-${PORTVERSION}
WRKSRC=		work/dfui

LIB_DEPENDS=	lua50.5:${PORTSDIR}/lang/lua50 \
		aura.3:${PORTSDIR}/devel/libaura \
		dfui.4:${PORTSDIR}/sysutils/libdfui

BUILD_DEPENDS=	${LOCALBASE}/bin/lua50c51:${PORTSDIR}/devel/lua50-compat51
RUN_DEPENDS=	${LOCALBASE}/bin/lua50c51:${PORTSDIR}/devel/lua50-compat51 \
		${LOCALBASE}/lib/lua/5.0/lposix.so:${PORTSDIR}/devel/lua50-posix

MAINTAINER=	cpressey@catseye.mine.nu
COMMENT=	DFUI binding for Lua 5.0.x

PLIST_FILES=	lib/lua/5.0/ldfui.so \
		share/lua/5.0/dfui.lua

do-fetch:
	@if [ ! -f ${_DISTDIR}${DISTNAME}${EXTRACT_SUFX} ]; then		\
		${ECHO_MSG};							\
		${ECHO_MSG} "Distfile ${DISTNAME}${EXTRACT_SUFX} not found!";	\
		${ECHO_MSG};							\
		${ECHO_MSG} "This is an 'INTERNAL' port, meaning that the distfile is not retrieved";	\
		${ECHO_MSG} "from the Internet, but rather created from a local checkout of the";	\
		${ECHO_MSG} "BSD Installer CVS repository.  Please see the shell scripts in the";	\
		${ECHO_MSG} "scripts/build directory of the repository for more information on how";	\
		${ECHO_MSG} "to create these distfiles.";			\
		false;								\
	fi

do-install:
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/ldfui.so ${LOCALBASE}/lib/lua/5.0/
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/dfui.lua ${LOCALBASE}/share/lua/5.0/

OSTYPE!=sysctl -n kern.ostype

.if ${OSTYPE} == "FreeBSD"
.include <bsd.port.mk>
.endif

.if ${OSTYPE} == "DragonFly"
.include <bsd.dfport.mk>
.endif
