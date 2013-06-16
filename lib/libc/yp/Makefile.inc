#	from: @(#)Makefile.inc	5.3 (Berkeley) 2/20/91
# $FreeBSD: src/lib/libc/yp/Makefile.inc,v 1.8 1999/08/28 00:02:58 peter Exp $

# yp sources
.PATH: ${.CURDIR}/../libc/yp

CMAPS+=	${.CURDIR}/yp/Symbol.map

SRCS+=	xdryp.c yp.h yp_xdr.c yplib.c
CLEANFILES+=	yp.h yp_xdr.c

RPCSRC=	${DESTDIR}/usr/include/rpcsvc/yp.x
RPCGEN=	rpcgen -C

yp_xdr.c: ${RPCSRC}
	${RPCGEN} -c -o ${.TARGET} ${RPCSRC}

yp.h: ${RPCSRC}
	${RPCGEN} -h -o ${.TARGET} ${RPCSRC}

.if ${LIB} == "c"
MAN+=	ypclnt.3

MLINKS+=ypclnt.3 yp_all.3 \
	ypclnt.3 yp_bind.3 \
	ypclnt.3 yp_first.3 \
	ypclnt.3 yp_get_default_domain.3 \
	ypclnt.3 yp_master.3 \
	ypclnt.3 yp_match.3 \
	ypclnt.3 yp_next.3 \
	ypclnt.3 yp_order.3 \
	ypclnt.3 yp_unbind.3 \
	ypclnt.3 yperr_string.3 \
	ypclnt.3 ypprot_err.3
.endif
