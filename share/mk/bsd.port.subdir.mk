# $FreeBSD: src/share/mk/bsd.port.subdir.mk,v 1.28.2.2 2002/07/17 19:08:23 ru Exp $
# $DragonFly: src/share/mk/Attic/bsd.port.subdir.mk,v 1.5 2005/04/20 21:58:13 okumoto Exp $

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

.include "${PORTSDIR}/Mk/bsd.port.subdir.mk"
