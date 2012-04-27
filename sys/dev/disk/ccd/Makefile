# $FreeBSD: src/sys/modules/ccd/Makefile,v 1.9 1999/11/28 18:52:42 bde Exp $ 

KMOD=   ccd
SRCS=   ccd.c use_ccd.h

NCCD?=          4

use_ccd.h:
	echo "#define NCCD ${NCCD}" > ${.TARGET}

.include <bsd.kmod.mk>
