# $FreeBSD: src/sys/modules/coff/Makefile,v 1.10 1999/11/28 18:52:45 bde Exp $
# $DragonFly: src/sys/emulation/ibcs2/coff/Attic/Makefile,v 1.3 2004/08/13 17:51:08 dillon Exp $

.PATH:	${.CURDIR}/../../i386/ibcs2
KMOD=	ibcs2_coff
SRCS= 	imgact_coff.c
NOMAN=
CFLAGS+=	-DCOMPAT_IBCS2
KMODDEPS=	ibcs2

.include <bsd.kmod.mk>
