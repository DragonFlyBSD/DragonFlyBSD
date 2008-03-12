# New ports collection makefile for:	dfuife_cgi
# Date created:			June 5, 2004
# Whom:				Chris Pressey <cpressey@catseye.mine.nu>

PORTNAME=	dfuife_cgi
PORTVERSION=    1.4
INTERNAL=	YES
CATEGORIES=	sysutils
DISTNAME=	${PORTNAME}-${PORTVERSION}
WRKSRC=		work/cgi

LIB_DEPENDS=	aura.3:${PORTSDIR}/devel/libaura \
		dfui.4:${PORTSDIR}/sysutils/libdfui

MAINTAINER=	cpressey@catseye.mine.nu
COMMENT=	DFUI CGI frontend

PLIST_FILES=	www/dfuife.cgi		\
		www/dfly-pg.gif		\
		www/fred-bg.gif		\
		www/fred.png		\
		www/dfuife.css		\
		www/dfuife.js		\
		www/index.html

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
		${INSTALL_PROGRAM} ${WRKSRC}/dfuife.cgi ${PREFIX}/www
		${INSTALL_DATA} ${WRKSRC}/dfly-pg.gif ${PREFIX}/www
		${INSTALL_DATA} ${WRKSRC}/fred-bg.gif ${PREFIX}/www
		${INSTALL_DATA} ${WRKSRC}/fred.png ${PREFIX}/www
		${INSTALL_DATA} ${WRKSRC}/dfuife.css ${PREFIX}/www
		${INSTALL_DATA} ${WRKSRC}/dfuife.js ${PREFIX}/www
		${INSTALL_DATA} ${WRKSRC}/index.html ${PREFIX}/www

OSTYPE!=sysctl -n kern.ostype

.if ${OSTYPE} == "FreeBSD"
.include <bsd.port.mk>
.endif

.if ${OSTYPE} == "DragonFly"
.include <bsd.dfport.mk>
.endif

