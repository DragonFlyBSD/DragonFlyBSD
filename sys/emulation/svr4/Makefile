# Makefile for syscall tables
#
# $FreeBSD: src/sys/svr4/Makefile,v 1.3 1999/09/01 00:32:18 newton Exp $
# $DragonFly: src/sys/emulation/svr4/Attic/Makefile,v 1.4 2003/08/07 21:17:19 dillon Exp $

MAINTAINER=	newton@freebsd.org

all:
	@echo "make svr4_sysent.c only"

svr4_sysent.c svr4_syscall.h svr4_proto.h svr4_union.h: ../../kern/makesyscalls.sh \
		syscalls.master syscalls.conf
	-mv -f svr4_sysent.c svr4_sysent.c.bak
	-mv -f svr4_syscall.h svr4_syscall.h.bak
	-mv -f svr4_proto.h svr4_proto.h.bak
	-mv -f svr4_union.h svr4_union.h.bak
	sh ../../kern/makesyscalls.sh syscalls.master syscalls.conf
