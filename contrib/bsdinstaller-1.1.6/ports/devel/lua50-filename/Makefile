# New ports collection makefile for: 	lua50-filename
# Date created:				28 Mar 2005
# Whom:					Chris Pressey

PORTNAME=	lua50-filename
PORTVERSION=	0.1
INTERNAL=	YES
CATEGORIES=	devel
DISTNAME=	luafilename-${PORTVERSION}
WRKSRC=		work/filename

LIB_DEPENDS=	lua50.5:${PORTSDIR}/lang/lua50
BUILD_DEPENDS=	${LOCALBASE}/bin/lua50c51:${PORTSDIR}/devel/lua50-compat51
RUN_DEPENDS=	${LOCALBASE}/bin/lua50c51:${PORTSDIR}/devel/lua50-compat51 \
		${LOCALBASE}/share/lua/5.0/posix.lua:${PORTSDIR}/devel/lua50-posix

MAINTAINER=	cpressey@catseye.mine.nu
COMMENT=	Filename-manipulating functions for Lua 5.0.x

PLIST_FILES=	share/lua/5.0/filename.lua

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

do-build:
	@${ECHO} "No building required."

do-install:
	${CP} ${WRKSRC}/filename.lua ${LOCALBASE}/share/lua/5.0/filename.lua

OSTYPE!=sysctl -n kern.ostype

.if ${OSTYPE} == "FreeBSD"
.include <bsd.port.mk>
.endif

.if ${OSTYPE} == "DragonFly"
.include <bsd.dfport.mk>
.endif
