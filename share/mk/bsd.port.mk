# $FreeBSD: src/share/mk/bsd.port.mk,v 1.303.2.2 2002/07/17 19:08:23 ru Exp $
# $DragonFly: src/share/mk/Attic/bsd.port.mk,v 1.15 2004/07/14 18:51:53 hmp Exp $

PORTSDIR?=	/usr/ports
DFPORTSDIR?=	/usr/dfports
PORTPATH!=	/usr/bin/relpath ${PORTSDIR} ${.CURDIR}

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

.if !exists(${DFPORTSDIR}/${PORTPATH}/Makefile)

.if defined(USE_RC_SUBR)
.undef USE_RC_SUBR
RC_SUBR=	/etc/rc.subr
.endif

# If the port does not exist in /usr/dfports/<portpath> use the original
# FreeBSD port.  Also process as per normal if BEFOREPORTMK is set so
# any expected variables are set.
#
.include <bsd.own.mk>
.include "${PORTSDIR}/Mk/bsd.port.mk"

.else

.undef BEFOREPORTMK
.undef AFTERPORTMK

# Otherwise retarget to the DragonFly override port.
#

TARGETS+=	all
TARGETS+=	build
TARGETS+=	checksum
TARGETS+=	clean
TARGETS+=	clean-for-cdrom
TARGETS+=	clean-for-cdrom-list
TARGETS+=	clean-restricted
TARGETS+=	clean-restricted-list
TARGETS+=	configure
TARGETS+=	deinstall
TARGETS+=	depend
TARGETS+=	depends
TARGETS+=	describe
TARGETS+=	distclean
TARGETS+=	extract
TARGETS+=	fetch
TARGETS+=	fetch-list
TARGETS+=	ignorelist
TARGETS+=	makesum
TARGETS+=	maintainer
TARGETS+=	package
TARGETS+=	realinstall
TARGETS+=	reinstall
TARGETS+=	install
TARGETS+=	tags

.undef PORTSDIR
.if !make(package-depends-list) && !make(all-depends-list) && \
    !make(run-depends-list) && !make(build-depends-list)
.BEGIN:
	@echo "WARNING, USING DRAGONFLY OVERRIDE ${DFPORTSDIR}/${PORTPATH}"
	cd ${DFPORTSDIR}/${PORTPATH} && ${MAKE} -B ${.TARGETS}
.endif

.if !empty(.TARGETS)
${.TARGETS}:
.else
all:
.MAIN: all
.endif

# Hack to get Makefiles with conditional statements working
XFREE86_VERSION?=	4
ARCH?=			i386
MACHINE_ARCH?=		i386
HAVE_GNOME?=

# WORKAROUND to get portupgrade working
# Taken from: ${PORTSDIR}/Mk/bsd.port.mk
.if !defined(PKGNAME)
PKGNAME=	${PKGNAMEPREFIX}${PORTNAME}${PKGNAMESUFFIX}-${PORTVERSION:C/[-_,]/./g}${_SUF1}${_SUF2}
.endif
 
.endif

