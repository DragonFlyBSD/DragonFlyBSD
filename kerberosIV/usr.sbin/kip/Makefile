# $FreeBSD: src/kerberosIV/usr.sbin/kip/Makefile,v 1.5 2000/02/24 20:57:01 markm Exp $
# $DragonFly: src/kerberosIV/usr.sbin/kip/Attic/Makefile,v 1.2 2003/06/17 04:26:25 dillon Exp $

PROG=		kip
NOMAN=		true
SRCS=		kip.c common.c
CFLAGS+=	-I${KRB4DIR}/include \
		-I${KRB4DIR}/lib/roken \
		-I${KRB4DIR}/lib/krb \
		-I${KRB4DIR}/kadmin \
		-I${KRBOBJDIR} \
		-I${.CURDIR}/../include
LDADD=		-L${ROKENOBJDIR} -lroken -L${KRBOBJDIR} -lkrb -lcrypto -lcrypt \
		-lcom_err
DPADD=		${LIBROKEN} ${LIBKRB} ${LIBCRYPTO} ${LIBCRYPT} ${LIBCOM_ERR}

.include <bsd.prog.mk>

.PATH:	${KRB4DIR}/appl/kip
