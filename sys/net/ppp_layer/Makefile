# $FreeBSD: src/sys/modules/if_ppp/Makefile,v 1.21 1999/11/28 18:52:51 bde Exp $

.PATH:  ${.CURDIR}/../ppp ${.CURDIR}/..
KMOD=   if_ppp
SRCS=	if_ppp.c ppp_tty.c slcompress.c \
	use_ppp.h opt_inet.h opt_ppp.h

NPPP?=		2
PPP_BSDCOMP?=	1	# 0/1
PPP_DEFLATE?=	1	# 0/1
PPP_FILTER?=	1	# 0/1 - requires bpf to be configured in kernel
PPP_INET?=	1	# 0/1 - requires INET to be configured in kernel

.if ${PPP_BSDCOMP} > 0
SRCS+=	bsd_comp.c
.endif
.if ${PPP_DEFLATE} > 0
SRCS+=	ppp_deflate.c zlib.c
.endif

use_ppp.h:
	echo "#define NPPP ${NPPP}" > ${.TARGET}

opt_inet.h:
	touch opt_inet.h
.if ${PPP_INET} > 0
	echo "#define INET 1" > ${.TARGET}
.endif

opt_ppp.h:
	touch ${.TARGET}
.if ${PPP_BSDCOMP} > 0
	echo "#define PPP_BSDCOMP ${PPP_BSDCOMP}" > ${.TARGET}
.endif
.if ${PPP_DEFLATE} > 0
	echo "#define PPP_DEFLATE ${PPP_DEFLATE}" >> ${.TARGET}
.endif
.if ${PPP_FILTER} > 0
	echo "#define PPP_FILTER ${PPP_FILTER}" >> ${.TARGET}
.endif

.include <bsd.kmod.mk>
