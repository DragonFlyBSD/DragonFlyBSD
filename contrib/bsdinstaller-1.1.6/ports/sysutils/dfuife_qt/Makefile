# New ports collection makefile for:	dfuife_qt
# Date created:			Oct 19, 2004
# Whom:				Chris Pressey <cpressey@catseye.mine.nu>

PORTNAME=	dfuife_qt
PORTVERSION=    1.2
INTERNAL=	YES
CATEGORIES=	sysutils
DISTNAME=	${PORTNAME}-${PORTVERSION}
WRKSRC=		work/qt

LIB_DEPENDS=	dfui.4:${PORTSDIR}/sysutils/libdfui

USE_QT_VER=	3

MAINTAINER=	cpressey@catseye.mine.nu
COMMENT=	DFUI Qt frontend

PLIST_FILES=	sbin/dfuife_qt

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
	${INSTALL_PROGRAM} -o root -g wheel -m 555 ${WRKSRC}/dfuife_qt ${LOCALBASE}/sbin

OSTYPE!=sysctl -n kern.ostype

.if ${OSTYPE} == "FreeBSD"
.include <bsd.port.mk>
.endif

.if ${OSTYPE} == "DragonFly"
.include <bsd.dfport.mk>
.endif

