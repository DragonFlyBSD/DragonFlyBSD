# $FreeBSD: src/kerberosIV/usr.bin/klist/Makefile,v 1.6.2.1 2001/04/25 09:36:03 ru Exp $
# $DragonFly: src/kerberosIV/usr.bin/klist/Attic/Makefile,v 1.2 2003/06/17 04:26:25 dillon Exp $

PROG=		klist
MAN=		${KRB4DIR}/man/klist.1
SRCS=		klist.c protos.h
CFLAGS+=	-I${KRB4DIR}/include \
		-I${KRB4DIR}/lib/roken \
		-I${KRB4DIR}/lib/kafs \
		-I${KRB4DIR}/lib/kadm \
		-I${KRB4DIR}/lib/kdb \
		-I${KRB4DIR}/lib/krb \
		-I${KRB4DIR}/kuser \
		-I${.OBJDIR}
LDADD=		-L${ROKENOBJDIR} -lroken \
		-L${KAFSOBJDIR} -lkafs -L${KRBOBJDIR} -lkrb -lcrypto -lcrypt \
		-lcom_err
DPADD=		${LIBROKEN} ${LIBKAFS} ${LIBKRB} ${LIBCRYPTO} ${LIBCRYPT} \
		${LIBCOM_ERR}

.include <bsd.prog.mk>

.PATH:	${KRB4DIR}/kuser
