# $FreeBSD: src/share/mk/bsd.port.subdir.mk,v 1.28.2.2 2002/07/17 19:08:23 ru Exp $
# $DragonFly: src/share/mk/Attic/bsd.port.subdir.mk,v 1.3 2004/04/25 11:02:18 joerg Exp $

PORTSDIR?=	/usr/ports

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

.include "${PORTSDIR}/Mk/bsd.port.subdir.mk"
