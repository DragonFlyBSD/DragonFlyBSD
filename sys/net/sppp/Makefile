# $FreeBSD: src/sys/modules/sppp/Makefile,v 1.5.2.1 2002/06/17 06:14:40 jhay Exp $

.PATH: ${.CURDIR}/../ppp_layer

KMOD=	sppp
SRCS=	if_spppsubr.c slcompress.c
SRCS+=	opt_inet.h opt_inet6.h

EXPORT_SYMS=	sppp_attach	\
		sppp_detach	\
		sppp_input	\
		sppp_ioctl	\
		sppp_dequeue	\
		sppp_pick	\
		sppp_isempty	\
		sppp_flush

.if !defined(BUILDING_WITH_KERNEL)
opt_inet.h:
	echo "#define INET 1" > ${.TARGET}

opt_inet6.h:
	echo "#define INET6 1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
