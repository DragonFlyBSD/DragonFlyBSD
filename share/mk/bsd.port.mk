# $FreeBSD: src/share/mk/bsd.port.mk,v 1.303.2.2 2002/07/17 19:08:23 ru Exp $
# $DragonFly: src/share/mk/Attic/bsd.port.mk,v 1.3 2003/09/08 23:39:23 dillon Exp $

PORTSDIR?=	/usr/ports
DPORTSDIR?=	/usr/dports
PORTPATH!=	/usr/bin/relpath ${PORTSDIR} ${.CURDIR}

.if !exists(${DPORTSDIR}/${PORTPATH}/Makefile)
# If the port does not exist in /usr/dports/<portpath> use the original
# FreeBSD port
#
.include <bsd.own.mk>
.include "${PORTSDIR}/Mk/bsd.port.mk"

.else

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

.for __target in ${TARGETS}
.if !target(${__target})
${__target}:
	@echo "WARNING, USING DRAGONFLY OVERRIDE ${DPORTSDIR}/${PORTPATH}"
	cd ${DPORTSDIR}/${PORTPATH} && ${MAKE} -B ${.TARGET}
.endif
.endfor

.endif

