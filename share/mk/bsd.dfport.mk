# $DragonFly: src/share/mk/Attic/bsd.dfport.mk,v 1.4 2004/02/24 13:05:14 joerg Exp $

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
.makeenv OSVERSION

.include <bsd.own.mk>
.include "${PORTSDIR}/Mk/bsd.port.mk"

