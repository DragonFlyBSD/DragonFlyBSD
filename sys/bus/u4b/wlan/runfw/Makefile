# $FreeBSD: src/sys/modules/runfw/Makefile,v 1.3 2012/11/17 01:52:59 svnexp Exp $

.include <bsd.dragonfly.mk>

KMOD=	runfw
FIRMWS=	runfw:runfw:1

CLEANFILES=	runfw

runfw: ${DF.SRCTOP}/sys/contrib/dev/run/rt2870.fw.uu
	${NORMAL_FW}

.include <bsd.kmod.mk>
