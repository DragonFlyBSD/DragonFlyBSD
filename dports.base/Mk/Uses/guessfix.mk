# $FreeBSD$
#
# Replace config.guess and config.sub files in specified directories
#
# MAINTAINER: draco@marino.st
#
# Feature:	guessfix
# Usage:	USES=guessfix
#

.if !defined(_INCLUDE_USES_GUESSFIX_Mk)
_INCLUDE_USES_GUESSFIX_MK=	yes

pre-patch: fix-subguess

fix-subguess:
	@cd ${WRKSRC}; ${FIND} * -type f -name config.sub -exec \
		${CP} ${PORTSDIR}/Templates/config.sub {} \;
	@cd ${WRKSRC}; ${FIND} * -type f -name config.guess -exec \
		${CP} ${PORTSDIR}/Templates/config.guess {} \;
.endif
