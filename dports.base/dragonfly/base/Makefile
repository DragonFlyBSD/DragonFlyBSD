PORTNAME=	dragonfly-base
PORTVERSION=	1.0
PORTREVISION=	1
CATEGORIES=	misc

# VALID_CATEGORIES+= dragonfly

MAINTAINER=	dillon@backplane.com
COMMENT=	DragonFlyBSD meta-port for base additions
WWW=		https://www.dragonflybsd.org/

USES=	metaport

COMPONENTS+=	ports-mgmt/pkg

.for comp in ${COMPONENTS}
RUN_DEPENDS:= ${RUN_DEPENDS} /:${comp}
.endfor

.include <bsd.port.mk>
