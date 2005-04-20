# $DragonFly: src/share/mk/Attic/bsd.dfport.mk,v 1.5 2005/04/20 21:58:13 okumoto Exp $

PORTSDIR?=	/usr/ports
DFPORTSDIR?=	/usr/dfports

# Temporary Hack
#
OSVERSION ?= 480102
UNAME_s?= FreeBSD
UNAME_v?=FreeBSD 4.8-CURRENT
UNAME_r?=4.8-CURRENT

.makeenv UNAME_s
.makeenv UNAME_v
.makeenv UNAME_r

.include <bsd.own.mk>
.include "${PORTSDIR}/Mk/bsd.port.mk"

