# Makefile for syscall tables
#
# $FreeBSD: src/sys/i386/linux/Makefile,v 1.4 1999/08/28 00:45:21 peter Exp $
# $DragonFly: src/sys/emulation/linux/i386/Makefile,v 1.2 2003/06/17 04:28:38 dillon Exp $

all:
	@echo "make linux_sysent.c only"

linux_sysent.c linux_syscall.h linux_proto.h: ../../kern/makesyscalls.sh \
		syscalls.master syscalls.conf
	-mv -f linux_sysent.c linux_sysent.c.bak
	-mv -f linux_syscall.h linux_syscall.h.bak
	-mv -f linux_proto.h linux_proto.h.bak
	sh ../../kern/makesyscalls.sh syscalls.master syscalls.conf
