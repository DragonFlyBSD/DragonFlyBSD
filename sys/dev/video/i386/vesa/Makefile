# $FreeBSD: src/sys/modules/vesa/Makefile,v 1.9 2000/01/29 15:08:42 peter Exp $
# $DragonFly: src/sys/dev/video/i386/vesa/Makefile,v 1.4 2004/08/28 17:21:22 dillon Exp $

.PATH:	${.CURDIR}/../../../misc/syscons ${.CURDIR}/../../i386/isa
KMOD	=	vesa
SRCS	=	vesa.c scvesactl.c opt_vga.h opt_vesa.h
NOMAN	=

.include <bsd.kmod.mk>
