# $FreeBSD: src/usr.bin/truss/Makefile,v 1.10.2.2 2002/07/23 09:18:30 ru Exp $

PROG=	truss
SRCS=	main.c setup.c syscalls.c syscalls.h ioctl.c ${MACHINE_ARCH}-fbsd.c

CFLAGS+= -I${.CURDIR} -I${.CURDIR}/../../sys
CFLAGS+= -I${.CURDIR}/../../sys/dev/drm/include -I.
CFLAGS+= -I${.CURDIR}/../../sys/contrib/dev/acpica/source/include

CLEANFILES+=syscalls.master syscalls.h ioctl.c

.SUFFIXES: .master

syscalls.master:	${.CURDIR}/../../sys/kern/syscalls.master
	cp ${.ALLSRC} syscalls.master

syscalls.h:	syscalls.master
	/bin/sh ${.CURDIR}/../../sys/kern/makesyscalls.sh syscalls.master \
		${.CURDIR}/i386.conf

ioctl.c!
	sh ${.CURDIR}/../kdump/mkioctls ${DESTDIR}/usr/include ${.CURDIR} > ${.TARGET}

.include <bsd.prog.mk>
