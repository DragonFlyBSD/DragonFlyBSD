# $FreeBSD: src/sys/modules/vn/Makefile,v 1.5.2.1 2001/07/24 09:49:43 dd Exp $ 

KMOD=   vn
SRCS=   vn.c use_vn.h

.if !defined(BUILDING_WITH_KERNEL)
use_vn.h:
	echo "#define	NVN	4" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
