# New ports collection makefile for:	dfuibe_installer
# Date created:			May 13, 2004
# Whom:				Chris Pressey <cpressey@catseye.mine.nu>

PORTNAME=	dfuibe_installer
PORTVERSION=    1.1.6
INTERNAL=	YES
CATEGORIES=	sysutils
DISTNAME=	${PORTNAME}-${PORTVERSION}
WRKSRC=		work/installer

LIB_DEPENDS=	aura.3:${PORTSDIR}/devel/libaura \
		dfui.4:${PORTSDIR}/sysutils/libdfui \
		installer.5:${PORTSDIR}/sysutils/libinstaller
.if defined(WITH_NLS)
LIB_DEPENDS:=	${LIB_DEPENDS} \
		intl.6:${PORTSDIR}/devel/gettext
.endif

MAINTAINER=	cpressey@catseye.mine.nu
COMMENT=	DFUI BSD Installer backend

SHAREDIR=	${LOCALBASE}/share/dfuibe_installer

.if defined(WITH_NLS)
LOCALEDIR=	${LOCALBASE}/share/locale
LANGUAGES=	ru
.endif

PLIST_FILES=	sbin/dfuibe_installer \
		share/dfuibe_installer/cmdnames.conf

.if defined(WITH_NLS)
PLIST_FILES+=	share/locale/ru/LC_MESSAGES/dfuibe_installer.mo
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
	${INSTALL_DATA} -s -o root -g wheel -m 555 ${WRKSRC}/dfuibe_installer ${LOCALBASE}/sbin
	${MKDIR} -p ${SHAREDIR}
	${INSTALL_DATA} -o root -g wheel -m 555 ${WRKSRC}/conf/cmdnames.conf ${SHAREDIR}
	for _language in ${LANGUAGES}; do \
		${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/po/$${_language}.mo \
		    ${LOCALEDIR}/$${_language}/LC_MESSAGES/${PORTNAME}.mo; \
	done

OSTYPE!=sysctl -n kern.ostype

.if ${OSTYPE} == "FreeBSD"
.include <bsd.port.mk>
.endif

.if ${OSTYPE} == "DragonFly"
.include <bsd.dfport.mk>
.endif

