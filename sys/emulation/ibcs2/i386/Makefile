# Makefile for syscall tables
#
# $FreeBSD: src/sys/i386/ibcs2/Makefile,v 1.2 1999/08/28 00:43:56 peter Exp $
# $DragonFly: src/sys/emulation/ibcs2/i386/Attic/Makefile,v 1.5 2003/08/15 06:32:48 dillon Exp $

all:
	@echo "make sysent only (generates ibcs2_sysent.c, ibcs2_isc_sysent.c and ibcs2_xenix_sysent.c)"

sysent:	ibcs2_sysent.c ibcs2_isc_sysent.c ibcs2_xenix_sysent.c

ibcs2_sysent.c ibcs2_syscall.h ibcs2_proto.h ibcs2_union.h: ../../../kern/makesyscalls.sh \
		syscalls.master syscalls.conf
	-mv -f ibcs2_sysent.c ibcs2_sysent.c.bak
	-mv -f ibcs2_syscall.h ibcs2_syscall.h.bak
	-mv -f ibcs2_proto.h ibcs2_proto.h.bak
	-mv -f ibcs2_union.h ibcs2_union.h.bak
	sh ../../../kern/makesyscalls.sh syscalls.master syscalls.conf

ibcs2_isc_sysent.c ibcs2_isc_syscall.h : ../../../kern/makesyscalls.sh \
		syscalls.isc syscalls.isc.conf
	-mv -f ibcs2_isc_sysent.c ibcs2_isc_sysent.c.bak
	-mv -f ibcs2_isc_syscall.h ibcs2_isc_syscall.h.bak
	sh ../../../kern/makesyscalls.sh syscalls.isc syscalls.isc.conf

ibcs2_xenix_sysent.c ibcs2_xenix_syscall.h ibcs2_xenix.h: \
		../../../kern/makesyscalls.sh syscalls.xenix syscalls.xenix.conf
	-mv -f ibcs2_xenix_sysent.c ibcs2_xenix_sysent.c.bak
	-mv -f ibcs2_xenix_syscall.h ibcs2_xenix_syscall.h.bak
	-mv -f ibcs2_xenix.h ibcs2_xenix.h.bak
	sh ../../../kern/makesyscalls.sh syscalls.xenix syscalls.xenix.conf
