# $FreeBSD: src/usr.sbin/fwcontrol/Makefile,v 1.1.2.5 2003/05/01 06:26:35 simokawa Exp $
# $DragonFly: src/usr.sbin/fwcontrol/Makefile,v 1.2 2003/06/17 04:29:53 dillon Exp $

PROG=	fwcontrol
SRCS=	fwcontrol.c fwcrom.c fwdv.c
MAN=	fwcontrol.8

.PATH:	${.CURDIR}/../../sys/dev/firewire

SDIR= ${.CURDIR}/../../sys
CFLAGS+=-I${.CURDIR} -I${SDIR}

.include <bsd.prog.mk>
