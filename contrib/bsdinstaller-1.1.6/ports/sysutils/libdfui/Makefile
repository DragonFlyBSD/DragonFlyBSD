# New ports collection makefile for:	libdfui
# Date created:			May 13, 2004
# Whom:				Chris Pressey <cpressey@catseye.mine.nu>

PORTNAME=	libdfui
PORTVERSION=    4.1
INTERNAL=	YES
CATEGORIES=	sysutils
DISTNAME=	${PORTNAME}-${PORTVERSION}
WRKSRC=		work/${PORTNAME}

LIB_DEPENDS=	aura.3:${PORTSDIR}/devel/libaura

MAINTAINER=	cpressey@catseye.mine.nu
COMMENT=	Abstract user interface protocol library

PLIST_FILES=	lib/libdfui.a			\
		lib/libdfui.so			\
		lib/libdfui.so.4		\
		include/dfui/dfui.h		\
		include/dfui/dump.h		\
		include/dfui/lang.h		\
		include/dfui/system.h

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
	${MKDIR} -p ${LOCALBASE}/include/dfui
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/libdfui.a ${LOCALBASE}/lib/
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/libdfui.so.4 ${LOCALBASE}/lib/
	${LN} -sf ${LOCALBASE}/lib/libdfui.so.4 ${LOCALBASE}/lib/libdfui.so
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/dfui.h ${LOCALBASE}/include/dfui/
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/dump.h ${LOCALBASE}/include/dfui/
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/lang.h ${LOCALBASE}/include/dfui/
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/system.h ${LOCALBASE}/include/dfui/

OSTYPE!=sysctl -n kern.ostype

.if ${OSTYPE} == "FreeBSD"
.include <bsd.port.mk>
.endif

.if ${OSTYPE} == "DragonFly"
.include <bsd.dfport.mk>
.endif

