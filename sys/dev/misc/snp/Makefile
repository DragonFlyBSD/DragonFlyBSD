# $FreeBSD: src/sys/modules/snp/Makefile,v 1.3.2.1 2002/03/11 01:24:44 dd Exp $

KMOD=	snp
SRCS=	snp.c use_snp.h

.if !defined(BUILDING_WITH_KERNEL)
use_snp.h:
	echo "#define	NSNP	4" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
