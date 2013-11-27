#	@(#)Makefile	8.2 (Berkeley) 3/21/94
# $FreeBSD: src/sys/kern/Makefile,v 1.6.2.1 2003/02/10 12:21:08 des Exp $

# Makefile for init_sysent.c

all:
	@echo "make sysent only"

sysent:	${.CURDIR}/init_sysent.c

${.CURDIR}/init_sysent.c ${.CURDIR}/syscalls.c \
${.CURDIR}/../sys/syscall.mk ${.CURDIR}/../sys/sysproto.h \
${.CURDIR}/../sys/sysunion.h: ${.CURDIR}/makesyscalls.sh ${.CURDIR}/syscalls.master
	-mv -f ${.CURDIR}/init_sysent.c ${.CURDIR}/init_sysent.c.bak
	-mv -f ${.CURDIR}/syscalls.c ${.CURDIR}/syscalls.c.bak
	-mv -f ${.CURDIR}/../sys/syscall.h ${.CURDIR}/../sys/syscall.h.bak
	-mv -f ${.CURDIR}/../sys/syscall.mk ${.CURDIR}/../sys/syscall.mk.bak
	-mv -f ${.CURDIR}/../sys/sysproto.h ${.CURDIR}/../sys/sysproto.h.bak
	-mv -f ${.CURDIR}/../sys/sysunion.h ${.CURDIR}/../sys/sysunion.h.bak
	cd ${.CURDIR} && sh makesyscalls.sh syscalls.master

SUBDIR= dsched firmware libmchain

.include <bsd.subdir.mk>
