#	@(#)Makefile	8.2 (Berkeley) 3/17/94
# $FreeBSD: src/sbin/disklabel/Makefile,v 1.3.14.3 2001/04/25 10:58:15 ru Exp $
# $DragonFly: src/sbin/disklabel/Makefile,v 1.2 2003/06/17 04:27:32 dillon Exp $

PROG=	disklabel
SRCS=	disklabel.c
MAN=	disklabel.5 disklabel.8

.if ${MACHINE} == "pc98"
CFLAGS+= -DPC98
.endif

.include <bsd.prog.mk>
