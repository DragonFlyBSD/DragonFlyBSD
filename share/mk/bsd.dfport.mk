# $DragonFly: src/share/mk/Attic/bsd.dfport.mk,v 1.3 2003/11/19 00:51:24 dillon Exp $

PORTSDIR?=	/usr/dfports

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

