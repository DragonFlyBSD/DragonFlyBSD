#	@(#)Makefile	8.2 (Berkeley) 3/21/94
# $FreeBSD: src/sys/kern/Makefile,v 1.6.2.1 2003/02/10 12:21:08 des Exp $
# $DragonFly: src/sys/kern/Makefile,v 1.5 2003/08/07 21:17:23 dillon Exp $

# Makefile for kernel tags files, init_sysent, etc.

ARCH=	i386 # luna68k news3400 pmax sparc tahoe vax

all:
	@echo "make tags, make links or init_sysent.c only"

init_sysent.c syscalls.c ../sys/syscall.h ../sys/syscall-hide.h \
../sys/syscall.mk ../sys/sysproto.h ../sys/sysunion.h: makesyscalls.sh syscalls.master
	-mv -f init_sysent.c init_sysent.c.bak
	-mv -f syscalls.c syscalls.c.bak
	-mv -f ../sys/syscall.h ../sys/syscall.h.bak
	-mv -f ../sys/syscall-hide.h ../sys/syscall-hide.h.bak
	-mv -f ../sys/syscall.mk ../sys/syscall.mk.bak
	-mv -f ../sys/sysproto.h ../sys/sysproto.h.bak
	-mv -f ../sys/sysunion.h ../sys/sysunion.h.bak
	sh makesyscalls.sh syscalls.master

# Kernel tags:
# Tags files are built in the top-level directory for each architecture,
# with a makefile listing the architecture-dependent files, etc.  The list
# of common files is in ./Make.tags.inc.  Links to the correct tags file
# are placed in each source directory.  We need to have links to tags files
# from the generic directories that are relative to the machine type, even
# via remote mounts; therefore we use symlinks to $SYSTAGS, which points at
# ${SYSDIR}/${MACHINE_ARCH}/tags.

SYSTAGS=/var/db/sys_tags
SYSDIR=${.CURDIR:H}
MFLAGS=SYS=${SYSDIR}

# Directories in which to place tags links (other than machine-dependent)
DGEN=	conf \
	dev \
	kern libkern \
	vfs vfs/deadfs vfs/fdesc vfs/fifofs \
	vfs/nullfs vfs/portal vfs/procfs \
	vfs/specfs vfs/umapfs vfs/union \
	vfs/ufs vfs/mfs vfs/nfs \
	net netinet netns sys \
	vm

tags::
	-for i in ${ARCH}; do \
	    (cd ../$$i && make ${MFLAGS} tags); done

links::
	rm -f ${SYSTAGS}
	-ln -s ${SYSDIR}/${MACHINE_ARCH}/tags ${SYSTAGS}
	-for i in ${DGEN}; do \
	    (cd ../$$i && { rm -f tags; ln -s ${SYSTAGS} tags; }) done
	-for i in ${ARCH}; do \
	    (cd ../$$i && make ${MFLAGS} SYSTAGS=${SYSTAGS} links); done
