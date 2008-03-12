# New ports collection makefile for:	dfuife_curses
# Date created:			May 13, 2004
# Whom:				Chris Pressey <cpressey@catseye.mine.nu>

PORTNAME=	dfuife_curses
PORTVERSION=    1.5
INTERNAL=	YES
CATEGORIES=	sysutils
DISTNAME=	${PORTNAME}-${PORTVERSION}
WRKSRC=		work/ncurses

LIB_DEPENDS=	aura.3:${PORTSDIR}/devel/libaura \
		dfui.4:${PORTSDIR}/sysutils/libdfui

.if defined(WITH_NLS)
LIB_DEPENDS:=	${LIB_DEPENDS} \
		intl.6:${PORTSDIR}/devel/gettext
.endif

MAINTAINER=	cpressey@catseye.mine.nu
COMMENT=	DFUI curses frontend

PLIST_FILES=	sbin/dfuife_curses share/dfuife_curses/fred.txt
.if defined(WITH_NLS)
PLIST_FILES+=	share/locale/ru/LC_MESSAGES/dfuife_curses.mo
.endif

.if defined(WITH_NLS)
LOCALEDIR=	${LOCALBASE}/share/locale
LANGUAGES=	ru
.endif

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
	${MKDIR} -p ${LOCALBASE}/share/dfuife_curses
	${INSTALL_PROGRAM} -o root -g wheel ${WRKSRC}/dfuife_curses ${LOCALBASE}/sbin/
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/fred.txt ${LOCALBASE}/share/dfuife_curses/
.if defined(WITH_NLS)
	for _language in ${LANGUAGES}; do \
		${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/po/$${_language}.mo \
		    ${LOCALEDIR}/$${_language}/LC_MESSAGES/${PORTNAME}.mo; \
	done
.endif

OSTYPE!=sysctl -n kern.ostype

.if ${OSTYPE} == "FreeBSD"
.include <bsd.port.mk>
.endif

.if ${OSTYPE} == "DragonFly"
.include <bsd.dfport.mk>
.endif

