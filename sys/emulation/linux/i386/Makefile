# Makefile for syscall tables
#
# $FreeBSD: src/sys/i386/linux/Makefile,v 1.4 1999/08/28 00:45:21 peter Exp $

all:
	@echo "make sysent only"

sysent: ${.CURDIR}/linux_sysent.c

${.CURDIR}/linux_sysent.c ${.CURDIR}/linux_syscall.h \
${.CURDIR}/linux_proto.h ${.CURDIR}/linux_union.h: \
	${.CURDIR}/../../../kern/makesyscalls.sh \
	${.CURDIR}/syscalls.master ${.CURDIR}/syscalls.conf
	-mv -f ${.CURDIR}/linux_sysent.c ${.CURDIR}/linux_sysent.c.bak
	-mv -f ${.CURDIR}/linux_syscall.h ${.CURDIR}/linux_syscall.h.bak
	-mv -f ${.CURDIR}/linux_proto.h ${.CURDIR}/linux_proto.h.bak
	-mv -f ${.CURDIR}/linux_union.h ${.CURDIR}/linux_union.h.bak
	cd ${.CURDIR} && sh ../../../kern/makesyscalls.sh syscalls.master syscalls.conf

SUBDIR=	linprocfs

.include <bsd.subdir.mk>
