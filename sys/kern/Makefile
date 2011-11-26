#	@(#)Makefile	8.2 (Berkeley) 3/21/94
# $FreeBSD: src/sys/kern/Makefile,v 1.6.2.1 2003/02/10 12:21:08 des Exp $
# $DragonFly: src/sys/kern/Makefile,v 1.13 2007/01/19 07:23:42 dillon Exp $

# Makefile for kernel tags files, init_sysent, etc.

TAGARCH= i386 # luna68k news3400 pmax sparc tahoe vax
SUBDIR= dsched firmware libmchain

# Kernel tags:
# Tags files are built in the top-level directory for each architecture,
# with a makefile listing the architecture-dependent files, etc.  The list
# of common files is in ./Make.tags.inc.  Links to the correct tags file
# are placed in each source directory.  We need to have links to tags files
# from the generic directories that are relative to the machine type, even
# via remote mounts; therefore we use symlinks to $SYSTAGS, which points at
# ${SYSDIR}/platform/${MACHINE_PLATFORM}/tags.

SYSTAGS=/var/db/sys_tags
SYSDIR=${.CURDIR:H}
MFLAGS=SYS=${SYSDIR}

# Directories in which to place tags links (other than machine-dependent)
DGEN=	conf \
	dev \
	kern libkern \
	vfs vfs/deadfs vfs/fdesc vfs/fifofs \
	vfs/nullfs vfs/portal vfs/procfs \
	vfs/specfs vfs/union \
	vfs/ufs vfs/mfs vfs/nfs vfs/tmpfs \
	net netinet sys \
	vm

.include <bsd.subdir.mk>

sysent:
	cd ${.CURDIR} && ${MAKE} -f Makefile.misc sysent

do_tags:
	cd ${.CURDIR} && ${MAKE} -f Makefile.misc tags

do_links:
	cd ${.CURDIR} && ${MAKE} -f Makefile.misc links

