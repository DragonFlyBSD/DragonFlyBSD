# Makefile for syscall tables
#
# $FreeBSD: src/sys/i386/linux/Makefile,v 1.4 1999/08/28 00:45:21 peter Exp $
# $DragonFly: src/sys/emulation/linux/i386/Makefile,v 1.5 2003/08/15 06:32:54 dillon Exp $

all:
	@echo "make sysent only"

sysent:	linux_sysent.c

linux_sysent.c linux_syscall.h linux_proto.h linux_union.h: ../../../kern/makesyscalls.sh \
		syscalls.master syscalls.conf
	-mv -f linux_sysent.c linux_sysent.c.bak
	-mv -f linux_syscall.h linux_syscall.h.bak
	-mv -f linux_proto.h linux_proto.h.bak
	-mv -f linux_union.h linux_union.h.bak
	sh ../../../kern/makesyscalls.sh syscalls.master syscalls.conf
