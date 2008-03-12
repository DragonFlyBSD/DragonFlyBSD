# New ports collection makefile for:	libaura
# Date created:			July 21, 2004
# Whom:				Chris Pressey <cpressey@catseye.mine.nu>

PORTNAME=	libaura
PORTVERSION=    3.1
INTERNAL=	YES
CATEGORIES=	sysutils
DISTNAME=	${PORTNAME}-${PORTVERSION}
WRKSRC=		work/${PORTNAME}

MAINTAINER=	cpressey@catseye.mine.nu
COMMENT=	Library of Assorted Useful Reusable Abstractions

PLIST_FILES=	lib/libaura.a			\
		lib/libaura.so			\
		lib/libaura.so.3		\
		include/aura/buffer.h		\
		include/aura/dict.h		\
		include/aura/fspred.h		\
		include/aura/popen.h		\
		include/aura/mem.h

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
	${MKDIR} -p ${LOCALBASE}/include/aura
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/libaura.a ${LOCALBASE}/lib/
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/libaura.so.3 ${LOCALBASE}/lib/
	${LN} -sf ${LOCALBASE}/lib/libaura.so.3 ${LOCALBASE}/lib/libaura.so
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/buffer.h ${LOCALBASE}/include/aura/
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/dict.h ${LOCALBASE}/include/aura/
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/popen.h ${LOCALBASE}/include/aura/
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/fspred.h ${LOCALBASE}/include/aura/
	${INSTALL_DATA} -o root -g wheel -m 444 ${WRKSRC}/mem.h ${LOCALBASE}/include/aura/

OSTYPE!=sysctl -n kern.ostype

.if ${OSTYPE} == "FreeBSD"
.include <bsd.port.mk>
.endif

.if ${OSTYPE} == "DragonFly"
.include <bsd.dfport.mk>
.endif

