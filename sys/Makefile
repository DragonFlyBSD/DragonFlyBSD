# $FreeBSD: src/sys/Makefile,v 1.20.2.1 2000/07/10 08:22:34 obrien Exp $
# $DragonFly: src/sys/Makefile,v 1.8 2003/10/24 17:22:15 dillon Exp $

# This is the old aout only boot loader.
.if	exists(${.CURDIR}/${MACHINE_ARCH}/boot) && ${OBJFORMAT} == "aout"
SUBDIR=	${MACHINE_ARCH}/boot
.elif	exists(${.CURDIR}/boot) && ${MACHINE_ARCH} == "i386" && ${OBJFORMAT} == "elf"
SUBDIR=	boot
.endif

.if	exists(${.CURDIR}/boot) && ${MACHINE_ARCH} == "alpha"
SUBDIR= boot
.endif

# KLD modules build for both a.out and ELF
# note: emulation must come before dev (for arch_svr4 softlink)
.if defined(MODULES_WITH_WORLD)
.if defined(MODULES_OVERRIDE)
SUBDIR+=${MODULES_OVERRIDE}
.else
SUBDIR+=bus checkpt crypto emulation dev kern net netgraph netproto vfs
.endif
.endif

HTAGSFLAGS+= -at `awk -F= '/^RELEASE *=/{release=$2}; END {print "FreeBSD", release, "kernel"}' < conf/newvers.sh`

.include <bsd.subdir.mk>

all_sysent:
	(cd ${.CURDIR}/kern; ${MAKE} sysent)
	(cd ${.CURDIR}/emulation/svr4; ${MAKE} sysent) 
	(cd ${.CURDIR}/emulation/linux/i386; ${MAKE} sysent) 
	(cd ${.CURDIR}/emulation/ibcs2/i386; ${MAKE} sysent) 

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

