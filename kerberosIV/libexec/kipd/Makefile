# $FreeBSD: src/kerberosIV/libexec/kipd/Makefile,v 1.5 2000/02/24 20:56:41 markm Exp $
# $DragonFly: src/kerberosIV/libexec/kipd/Attic/Makefile,v 1.2 2003/06/17 04:26:25 dillon Exp $

PROG=		kipd
NOMAN=		yes
SRCS=		kipd.c common.c
CFLAGS+=	-I${KRB4DIR}/include \
		-I${KRB4DIR}/lib/roken \
		-I${KRB4DIR}/lib/krb \
		-I${KRB4DIR}/appl/kip
LDADD=		-L${ROKENOBJDIR} -lroken -L${KRBOBJDIR} -lkrb -lcrypto -lcrypt \
		-lcom_err
DPADD=		${LIBROKEN} ${LIBKRB} ${LIBCRYPTO} ${LIBCRYPT} ${LIBCOM_ERR}

.include <bsd.prog.mk>

.PATH:	${KRB4DIR}/appl/kip
