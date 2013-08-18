# $FreeBSD: src/sys/modules/runfw/Makefile,v 1.3 2012/11/17 01:52:59 svnexp Exp $

KMOD=	runfw
FIRMWS=	runfw:runfw:1

CLEANFILES=	runfw

runfw: ${.CURDIR}/../../../../contrib/dev/run/rt2870.fw.uu
	uudecode -p ${.CURDIR}/../../../../contrib/dev/run/rt2870.fw.uu > ${.TARGET}

.include <bsd.kmod.mk>
