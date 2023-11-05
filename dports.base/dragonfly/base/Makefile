PORTNAME=	dragonfly-base
PORTVERSION=	1.0
PORTREVISION=	1
CATEGORIES=	misc

# VALID_CATEGORIES+= dragonfly

MAINTAINER=	dillon@backplane.com
COMMENT=	DragonFlyBSD meta-port for base additions
WWW=		https://www.dragonflybsd.org/

USES=	metaport

COMPONENTS+=	ports-mgmt/pkg \
		security/ca_root_nss \
		devel/git-lite \
		dns/bind-tools \
		net/isc-dhcp44-server \
		sysutils/cdrtools

.for comp in ${COMPONENTS}
prefix=${comp:C/.*\///}
RUN_DEPENDS:= ${RUN_DEPENDS} ${prefix}>0:${comp}
.endfor

.include <bsd.port.mk>
