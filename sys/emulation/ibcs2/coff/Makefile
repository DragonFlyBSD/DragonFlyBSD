# $FreeBSD: src/sys/modules/coff/Makefile,v 1.10 1999/11/28 18:52:45 bde Exp $
# $DragonFly: src/sys/emulation/ibcs2/coff/Attic/Makefile,v 1.2 2003/06/17 04:28:43 dillon Exp $

.PATH:	${.CURDIR}/../../i386/ibcs2
KMOD=	ibcs2_coff
SRCS= 	imgact_coff.c vnode_if.h
NOMAN=
CFLAGS+=	-DCOMPAT_IBCS2
KMODDEPS=	ibcs2

.include <bsd.kmod.mk>
