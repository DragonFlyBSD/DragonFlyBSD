# $FreeBSD: src/sys/Makefile,v 1.20.2.1 2000/07/10 08:22:34 obrien Exp $

# The boot loader
.if	exists(${.CURDIR}/boot)
SUBDIR=	boot
.endif

# Loadable kernel modules
#
.if defined(MODULES_WITH_WORLD)
.if defined(MODULES_OVERRIDE)
SUBDIR+=${MODULES_OVERRIDE}
.else
SUBDIR+=bus crypto dev emulation gnu kern net netbt netproto vfs
.if defined(WANT_NETGRAPH7)
SUBDIR+=netgraph7
.else
SUBDIR+=netgraph
.endif
SUBDIR+=libiconv
.endif
.endif

.include <bsd.subdir.mk>

all_sysent:
	(cd ${.CURDIR}/kern; ${MAKE} sysent)

# XXX this may be temporary, should the module build be incorporated
# into the default build (make the SUBDIR additions above non-conditional)?
#
modules:
	cd ${.CURDIR} && ${MAKE} -f Makefile.modules

mobj:
	cd ${.CURDIR} && ${MAKE} -f Makefile.modules obj

mclean:
	cd ${.CURDIR} && ${MAKE} -f Makefile.modules clean

mcleanobj:
	cd ${.CURDIR} && ${MAKE} -f Makefile.modules cleanobj

minstall:
	cd ${.CURDIR} && ${MAKE} -f Makefile.modules install

