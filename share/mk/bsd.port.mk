# $FreeBSD: src/share/mk/bsd.port.mk,v 1.303.2.2 2002/07/17 19:08:23 ru Exp $
# $DragonFly: src/share/mk/Attic/bsd.port.mk,v 1.6 2003/10/13 23:57:41 dillon Exp $

PORTSDIR?=	/usr/ports
DFPORTSDIR?=	/usr/dfports
PORTPATH!=	/usr/bin/relpath ${PORTSDIR} ${.CURDIR}

.if !exists(${DFPORTSDIR}/${PORTPATH}/Makefile) || defined(BEFOREPORTMK)
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
.BEGIN:
	@echo "WARNING, USING DRAGONFLY OVERRIDE ${DFPORTSDIR}/${PORTPATH}"
	cd ${DFPORTSDIR}/${PORTPATH} && ${MAKE} -B ${.TARGETS}

.endif

