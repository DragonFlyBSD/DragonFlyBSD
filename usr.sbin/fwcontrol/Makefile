# $FreeBSD: src/usr.sbin/fwcontrol/Makefile,v 1.1.2.5 2003/05/01 06:26:35 simokawa Exp $
# $DragonFly: src/usr.sbin/fwcontrol/Makefile,v 1.3 2003/08/08 04:18:44 dillon Exp $

PROG=	fwcontrol
SRCS=	fwcontrol.c fwcrom.c fwdv.c
MAN=	fwcontrol.8

.PATH:	${.CURDIR}/../../sys/bus/firewire

SDIR= ${.CURDIR}/../../sys
CFLAGS+=-I${.CURDIR} -I${SDIR}

.include <bsd.prog.mk>
