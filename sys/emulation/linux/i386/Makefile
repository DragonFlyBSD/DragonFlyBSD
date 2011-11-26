# Makefile for syscall tables
#
# $FreeBSD: src/sys/i386/linux/Makefile,v 1.4 1999/08/28 00:45:21 peter Exp $

SUBDIR=	linprocfs

.include <bsd.subdir.mk>

sysent:
	cd ${.CURDIR} && ${MAKE} -f Makefile.misc sysent

