#
# $FreeBSD: src/sys/i386/ibcs2/Makefile,v 1.2 1999/08/28 00:43:56 peter Exp $
# $DragonFly: src/sys/emulation/ibcs2/i386/Attic/Makefile,v 1.6 2003/08/21 20:08:28 dillon Exp $

all:
	@echo "make sysent only (generates ibcs2_sysent.c, ibcs2_isc_sysent.c and ibcs2_xenix_sysent.c)"

sysent:
	cd ${.CURDIR} && ${MAKE} -f Makefile.misc sysent

