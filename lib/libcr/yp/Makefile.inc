#	from: @(#)Makefile.inc	5.3 (Berkeley) 2/20/91
# $FreeBSD: src/lib/libc/yp/Makefile.inc,v 1.8 1999/08/28 00:02:58 peter Exp $
# $DragonFly: src/lib/libcr/yp/Attic/Makefile.inc,v 1.3 2003/07/24 21:41:57 dillon Exp $

# yp sources
.PATH: ${.CURDIR}/../libcr/yp

SRCS+=	xdryp.c yp.h yp_xdr.c yplib.c
CLEANFILES+=	yp.h yp_xdr.c

RPCSRC=	${DESTDIR}/usr/include/rpcsvc/yp.x
RPCGEN=	rpcgen -C

yp_xdr.c: ${RPCSRC}
	${RPCGEN} -c -o ${.TARGET} ${RPCSRC}

yp.h: ${RPCSRC}
	${RPCGEN} -h -o ${.TARGET} ${RPCSRC}
