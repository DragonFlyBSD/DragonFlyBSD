# $FreeBSD: src/kerberosIV/usr.bin/kauth/Makefile,v 1.5.2.1 2001/04/25 09:36:01 ru Exp $
# $DragonFly: src/kerberosIV/usr.bin/kauth/Attic/Makefile,v 1.2 2003/06/17 04:26:25 dillon Exp $

PROG=		kauth
MAN=		${KRB4DIR}/man/kauth.1
SRCS=		kauth.c rkinit.c encdata.c marshall.c
CFLAGS+=	-I${KRB4DIR}/include \
		-I${KRB4DIR}/lib/roken \
		-I${KRB4DIR}/lib/kafs \
		-I${KRB4DIR}/lib/krb \
		-I${KRB4DIR}/appl/kauth
LDADD=		-L${ROKENOBJDIR} -lroken -L${KAFSOBJDIR} -lkafs \
		-L${KRBOBJDIR} -lkrb -lcrypto -lcrypt -lcom_err
DPADD=		${LIBROKEN} ${LIBKAFS} ${LIBKRB} ${LIBCRYPTO} ${LIBCRYPT} \
		${LIBCOM_ERR}

.include <bsd.prog.mk>

.PATH:	${KRB4DIR}/appl/kauth
