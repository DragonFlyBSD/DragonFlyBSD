# New ports collection makefile for:	libinstaller
# Date created:			Aug 8, 2004
# Whom:				Chris Pressey <cpressey@catseye.mine.nu>

PORTNAME=	libinstaller
PORTVERSION=    5.1
INTERNAL=	YES
CATEGORIES=	sysutils
DISTNAME=	${PORTNAME}-${PORTVERSION}
WRKSRC=		work/${PORTNAME}

LIB_DEPENDS=	aura.3:${PORTSDIR}/devel/libaura \
		dfui.4:${PORTSDIR}/sysutils/libdfui

MAINTAINER=	cpressey@catseye.mine.nu
COMMENT=	BSD Installer library

PLIST_FILES=	lib/libinstaller.a		\
		lib/libinstaller.so		\
		lib/libinstaller.so.5		\
		include/installer/commands.h	\
		include/installer/confed.h	\
		include/installer/diskutil.h	\
		include/installer/functions.h	\
		include/installer/package.h	\
		include/installer/uiutil.h

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
	${MKDIR} -p ${LOCALBASE}/include/installer
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/libinstaller.a ${LOCALBASE}/lib/
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/libinstaller.so.5 ${LOCALBASE}/lib/
	${LN} -sf ${LOCALBASE}/lib/libinstaller.so.5 ${LOCALBASE}/lib/libinstaller.so
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/commands.h ${LOCALBASE}/include/installer/
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/confed.h ${LOCALBASE}/include/installer/
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/diskutil.h ${LOCALBASE}/include/installer/
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/functions.h ${LOCALBASE}/include/installer/
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/package.h ${LOCALBASE}/include/installer/
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/uiutil.h ${LOCALBASE}/include/installer/

OSTYPE!=sysctl -n kern.ostype

.if ${OSTYPE} == "FreeBSD"
.include <bsd.port.mk>
.endif

.if ${OSTYPE} == "DragonFly"
.include <bsd.dfport.mk>
.endif

